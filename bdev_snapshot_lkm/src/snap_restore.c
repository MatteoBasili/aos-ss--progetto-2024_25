#include <linux/blkdev.h>
#include <linux/crc32.h>
#include <linux/sort.h>

#include "snap_restore.h"
#include "snap_store.h"
#include "snap_utils.h"
#include "uapi/bdev_snapshot.h"

static DEFINE_MUTEX(device_restore_mutex);

/* -------------------------------------------------------------------
 * Directory iteration callbacks
 * ------------------------------------------------------------------- */

/**
 * snap_list_actor_filldir - Callback for iterate_dir() when listing snapshots
 * @ctx:     Directory context
 * @name:    Entry name
 * @namelen: Entry name length
 * @offset:  Directory offset
 * @ino:     Inode number
 * @d_type:  Entry type
 *
 * Filters directory entries and stores valid snapshot timestamps.
 */
static bool snap_list_actor_filldir(struct dir_context *ctx,
                                    const char *name, int namelen,
                                    loff_t offset, u64 ino,
                                    unsigned int d_type)
{
    struct snap_list_ctx *sctx = container_of(ctx, struct snap_list_ctx, ctx);
    size_t dev_len = strlen(sctx->dev_sanitized);
    size_t ts_len;

    if (sctx->count >= MAX_SNAPSHOTS)
        return true;

    /* Expected format: "<dev>_<timestamp>" */
    if (namelen <= dev_len + 1)
        return true;

    if (strncmp(name, sctx->dev_sanitized, dev_len) != 0)
        return true;

    if (name[dev_len] != '_')
        return true;

    ts_len = namelen - (dev_len + 1);
    if (ts_len >= SNAP_TIMESTAMP_MAX)
        ts_len = SNAP_TIMESTAMP_MAX - 1;
           
    memcpy(sctx->timestamps[sctx->count], name + dev_len + 1, ts_len);
    sctx->timestamps[sctx->count][ts_len] = '\0';

    sctx->count++;
    return true;
}

/**
 * list_snapshots_for_device - List all snapshots for a given device
 */
int list_snapshots_for_device(const char *dev_name,
                              char timestamps[MAX_SNAPSHOTS][SNAP_TIMESTAMP_MAX],
                              int *count)
{
    struct file *dir = NULL;
    struct snap_list_ctx ctx;
    int ret;

    if (!dev_name || !timestamps || !count)
        return -EINVAL;

    sanitize_devname(dev_name, ctx.dev_sanitized, DEV_NAME_LEN_MAX);

    ctx.count = 0;
    ctx.timestamps = timestamps;
    ctx.ctx.actor = snap_list_actor_filldir;
    ctx.ctx.pos = 0;

    dir = filp_open(SNAP_ROOT_DIR, O_RDONLY | O_DIRECTORY, 0);
    if (IS_ERR(dir))
        return PTR_ERR(dir);

    ret = iterate_dir(dir, &ctx.ctx);
    filp_close(dir, NULL);

    *count = ctx.count;

    if (ctx.count > 1) {
        sort(timestamps, ctx.count, SNAP_TIMESTAMP_MAX, cmp_timestamps_desc, NULL);
    }

    return (ctx.count == 0) ? -ENOENT : ret;
}

/* -------------------------------------------------------------------
 * Legge metadata.json e popola struct temporanea
 * Controlla se snapshot è aperta e ritorna -EBUSY se open==1
 * ------------------------------------------------------------------- */
int snap_load_metadata(struct snap_restore_tmp *dev, const char *snap_dir)
{
    struct file *filp;
    loff_t pos = 0;
    loff_t size;
    char *buf = NULL;
    char *p;
    int ret = 0;

    if (!dev || !snap_dir)
        return -EINVAL;

    char *path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!path)
        return -ENOMEM;

    snprintf(path, PATH_MAX, "%s/%s/metadata.json", SNAP_ROOT_DIR, snap_dir);

    filp = filp_open(path, O_RDONLY, 0);
    kfree(path);
    if (IS_ERR(filp))
        return PTR_ERR(filp);

    size = i_size_read(file_inode(filp));
    if (size <= 0) {
        filp_close(filp, NULL);
        return -EINVAL;
    }

    buf = kmalloc(size + 1, GFP_KERNEL);
    if (!buf) {
        filp_close(filp, NULL);
        return -ENOMEM;
    }

    if (kernel_read(filp, buf, size, &pos) != size) {
        kfree(buf);
        filp_close(filp, NULL);
        return -EIO;
    }
    buf[size] = '\0';
    filp_close(filp, NULL);

    /* Controlla se snapshot è aperta */
    p = strnstr(buf, "\"open\":", size);
    if (!p) {
        pr_err("%s: cannot find open flag in metadata.json\n", MOD_NAME);
        ret = -EINVAL;
        goto out;
    }
    int open_flag = 0;
    sscanf(p, "\"open\": %d", &open_flag);
    if (open_flag == 1) {
        pr_err("%s: snapshot %s is currently open\n", MOD_NAME, snap_dir);
        ret = -EBUSY;
        goto out;
    }

    /* Leggi block_size */
    p = strnstr(buf, "\"block_size\":", size);
    if (!p) {
        pr_err("%s: cannot find block_size in metadata.json\n", MOD_NAME);
        ret = -EINVAL;
        goto out;
    }
    sscanf(p, "\"block_size\": %llu", (unsigned long long *)&dev->block_size);

    /* Leggi num_blocks */
    p = strnstr(buf, "\"num_blocks\":", size);
    if (!p) {
        pr_err("%s: cannot find num_blocks in metadata.json\n", MOD_NAME);
        ret = -EINVAL;
        goto out;
    }
    sscanf(p, "\"num_blocks\": %llu", (unsigned long long *)&dev->num_blocks);

    /* Leggi lista dei blocchi */
    p = strnstr(buf, "\"blocks\": [", size);
    if (!p) {
        pr_err("%s: cannot find blocks array in metadata.json\n", MOD_NAME);
        ret = -EINVAL;
        goto out;
    }
    p += strlen("\"blocks\": [");

    /* Conta quanti blocchi ci sono */
    dev->num_saved_blocks = 0;
    for (char *q = p; *q && *q != ']'; q++)
        if (*q == ',')
            dev->num_saved_blocks++;
    if (*(p) != ']')
        dev->num_saved_blocks++; 

    if (dev->num_saved_blocks > 0) {
        dev->saved_blocks = kmalloc_array(dev->num_saved_blocks, sizeof(u64), GFP_KERNEL);
        if (!dev->saved_blocks) {
            ret = -ENOMEM;
            goto out;
        }

        /* Parse dei blocchi */
        int idx = 0;
        u64 block;
        while (sscanf(p, " %llu", (unsigned long long *)&block) == 1) {
            dev->saved_blocks[idx++] = block;
            p = strchr(p, ',');
            if (!p) break;
            p++; // salta la virgola
        }
    }

out:
    kfree(buf);
    return ret;
}

/* -------------------------------------------------------------------
 * Libera la memoria della struct temporanea
 * ------------------------------------------------------------------- */
void snap_free_metadata(struct snap_restore_tmp *dev)
{
    if (!dev)
        return;
    kfree(dev->saved_blocks);
    dev->saved_blocks = NULL;
    dev->num_saved_blocks = 0;
}

/* -------------------------------------------------------------------
 * Restore snapshot: scrive i blocchi salvati sul device reale
 * ------------------------------------------------------------------- */
int restore_snapshot_for_device(const char *dev_name, const char *timestamp)
{
    struct snap_restore_tmp dev = {0};
    struct file *dev_file = NULL;
    char *block_path = NULL;
    int ret = 0;
    int i;

    if (!dev_name || !timestamp)
        return -EINVAL;

    /* Costruisci nome della snapshot */
    char *snap_dir = kmalloc(PATH_MAX, GFP_KERNEL);
    char *dev_sanitized = kmalloc(DEV_NAME_LEN_MAX, GFP_KERNEL);
    if (!snap_dir || !dev_sanitized) {
        ret = -ENOMEM;
        goto out_free_heap;
    }

    sanitize_devname(dev_name, dev_sanitized, DEV_NAME_LEN_MAX);
    snprintf(snap_dir, PATH_MAX, "%s_%s", dev_sanitized, timestamp);

    /* Carica metadata.json */
    ret = snap_load_metadata(&dev, snap_dir);
    if (ret) {
        if (ret == -EBUSY)
            pr_err("%s: snapshot %s is currently open\n", MOD_NAME, snap_dir);
        else
            pr_err("%s: failed to load metadata for %s\n", MOD_NAME, dev_name);
        goto out_free_heap;
    }

    mutex_lock(&device_restore_mutex);

    /* Apri device reale */
    dev_file = filp_open(dev_name, O_WRONLY | O_LARGEFILE, 0);
    if (IS_ERR(dev_file)) {
        ret = PTR_ERR(dev_file);
        goto out_unlock_free_metadata;
    }

    block_path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!block_path) {
        ret = -ENOMEM;
        goto out_close_dev;
    }

    /* Cicla sui blocchi salvati */
    for (i = 0; i < dev.num_saved_blocks; i++) {
        struct file *blk_file = NULL;
        void *buf = NULL;
        loff_t dev_pos;
        loff_t pos = 0;
        u64 block_num = dev.saved_blocks[i];
        size_t block_size = dev.block_size;
        ssize_t read_bytes;

        /* Alloca buffer */
        buf = kmalloc(block_size, GFP_KERNEL);
        if (!buf) {
            ret = -ENOMEM;
            goto out_loop;
        }

        /* Apri file block_XXXXXXXX */
        snprintf(block_path, PATH_MAX, "%s/%s/block_%08llu", SNAP_ROOT_DIR, snap_dir,
                 (unsigned long long)block_num);
                 
        blk_file = filp_open(block_path, O_RDONLY, 0);
        if (IS_ERR(blk_file)) {
            pr_warn("%s: cannot open block file %s\n", MOD_NAME, block_path);
            kfree(buf);
            continue; // salta blocco mancante
        }

        /* Leggi blocco */
        read_bytes = kernel_read(blk_file, buf, block_size, &pos);
        filp_close(blk_file, NULL);
        if (read_bytes != block_size) {
            pr_warn("%s: incomplete read for block %llu\n", MOD_NAME, block_num);
            kfree(buf);
            continue;
        }

        /* Scrivi sul device reale */
        dev_pos = block_num * block_size;
        read_bytes = kernel_write(dev_file, buf, block_size, &dev_pos);
        if (read_bytes != block_size) {
            pr_err("%s: failed to write block %llu to device\n", MOD_NAME, block_num);
            kfree(buf);
            ret = -EIO;
            goto out_loop;
        }

        kfree(buf);
    }

out_loop:
    kfree(block_path);
out_close_dev:
    filp_close(dev_file, NULL);
out_unlock_free_metadata:
    mutex_unlock(&device_restore_mutex);
    snap_free_metadata(&dev);
out_free_heap:
    kfree(snap_dir);
    kfree(dev_sanitized);
    return ret;
}

