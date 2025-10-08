#include <linux/blkdev.h>
#include <linux/sort.h>

#include "snap_restore.h"
#include "snap_store.h"
#include "snap_utils.h"

/* -------------------------------------------------------------------
 * Directory iteration callback
 * ------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------
 * List all snapshots for a given device
 * -------------------------------------------------------------------- */
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
 * Reads metadata.json and populates temporary restore struct
 * ------------------------------------------------------------------- */
int snap_load_metadata(struct snap_restore_tmp *dev, const char *snap_dir)
{
    struct file *filp = NULL;
    loff_t pos = 0;
    loff_t size;
    char *buf = NULL;
    int ret = 0;

    if (!dev || !snap_dir)
        return -EINVAL;

    memset(dev, 0, sizeof(*dev));

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
        ret = -EINVAL;
        goto out_close;
    }

    buf = kmalloc(size + 1, GFP_KERNEL);
    if (!buf) {
        ret = -ENOMEM;
        goto out_close;
    }

    if (kernel_read(filp, buf, size, &pos) != size) {
        ret = -EIO;
        goto out_free;
    }
    buf[size] = '\0';

    if (sscanf(buf, "%*[^0123456789]\"magic\": 0x%X", &dev->magic) != 1 ||
        sscanf(buf, "%*[^0123456789]\"version\": %hu", &dev->version) != 1 ||
        sscanf(buf, "%*[^0123456789]\"block_size\": %llu", (unsigned long long *)&dev->block_size) != 1 ||
        sscanf(buf, "%*[^0123456789]\"num_blocks\": %llu", (unsigned long long *)&dev->num_blocks) != 1) {
        pr_err("%s: failed to parse metadata fields\n", MOD_NAME);
        ret = -EINVAL;
        goto out_free;
    }

    /* Check open flag */
    int open_flag = 0;
    if (sscanf(buf, "%*[^0123456789]\"open\": %d", &open_flag) != 1) {
        pr_err("%s: cannot find open flag\n", MOD_NAME);
        ret = -EINVAL;
        goto out_free;
    }
    if (open_flag == 1) {
        ret = -EBUSY;
        goto out_free;
    }

    char *blocks_start = strnstr(buf, "\"blocks\": [", size);
    if (!blocks_start) {
        dev->num_saved_blocks = 0;
        goto out_free;
    }
    blocks_start += strlen("\"blocks\": [");

    dev->num_saved_blocks = 0;
    for (char *q = blocks_start; *q && *q != ']'; q++)
        if (*q == ',')
            dev->num_saved_blocks++;
    if (*blocks_start != ']')
        dev->num_saved_blocks++;

    if (dev->num_saved_blocks > 0) {
        dev->saved_blocks = kmalloc_array(dev->num_saved_blocks, sizeof(u64), GFP_KERNEL);
        if (!dev->saved_blocks) {
            ret = -ENOMEM;
            goto out_free;
        }

        int idx = 0;
        u64 block;
        char *p = blocks_start;
        while (sscanf(p, " %llu", (unsigned long long *)&block) == 1) {
            if (idx >= dev->num_saved_blocks)
                break;
            dev->saved_blocks[idx++] = block;
            p = strchr(p, ',');
            if (!p) break;
            p++;
        }
    }

out_free:
    kfree(buf);
out_close:
    filp_close(filp, NULL);
    if (ret && dev->saved_blocks) {
        kfree(dev->saved_blocks);
        dev->saved_blocks = NULL;
    }
    return ret;
}

/* -------------------------------------------------------------------
 * Frees the temporary restore struct's memory
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
 * Restore snapshot: writes the saved blocks to the device file
 * ------------------------------------------------------------------- */
static int restore_snapshot_for_device_file(const char *dev_name, const char *timestamp)
{
    struct snap_restore_tmp dev = {0};
    struct file *dev_file = NULL;
    char *snap_dir = NULL;
    char *dev_sanitized = NULL;
    char *block_path = NULL;
    void *buf = NULL;
    int ret = 0;
    int i;

    if (!dev_name || !timestamp)
        return -EINVAL;

    /* Path snapshot preparation */
    snap_dir = kmalloc(PATH_MAX, GFP_KERNEL);
    dev_sanitized = kmalloc(DEV_NAME_LEN_MAX, GFP_KERNEL);
    if (!snap_dir || !dev_sanitized) {
        ret = -ENOMEM;
        goto out_free_heap;
    }

    sanitize_devname(dev_name, dev_sanitized, DEV_NAME_LEN_MAX);
    snprintf(snap_dir, PATH_MAX, "%s_%s", dev_sanitized, timestamp);

    /* Load metadata */
    ret = snap_load_metadata(&dev, snap_dir);
    if (ret) {
        if (ret == -EBUSY)
            pr_err("%s: snapshot %s is currently open\n", MOD_NAME, snap_dir);
        else
            pr_err("%s: failed to load metadata for %s\n", MOD_NAME, dev_name);
        goto out_free_heap;
    }
    
    if (dev.magic != SNAP_MAGIC || dev.version != SNAP_VERSION) {
        pr_err("%s: incompatible snapshot format (magic/version mismatch)\n", MOD_NAME);
        ret = -EINVAL;
        goto out_free_metadata;
    }

    static DEFINE_MUTEX(device_mutex);
    mutex_lock(&device_mutex);

    /* Open device file */
    dev_file = filp_open(dev_name, O_WRONLY | O_LARGEFILE, 0);
    if (IS_ERR(dev_file)) {
        ret = PTR_ERR(dev_file);
        pr_err("%s: cannot open device %s (err=%d)\n", MOD_NAME, dev_name, ret);
        goto out_unlock_metadata;
    }

    block_path = kmalloc(PATH_MAX, GFP_KERNEL);
    buf = kmalloc(dev.block_size, GFP_KERNEL);
    if (!block_path || !buf) {
        ret = -ENOMEM;
        goto out_close_dev;
    }

    /* Loop through all blocks */
    for (i = 0; i < dev.num_saved_blocks; i++) {
        struct file *blk_file = NULL;
        loff_t dev_pos;
        loff_t pos = 0;
        u64 block_num = dev.saved_blocks[i];
        
        snprintf(block_path, PATH_MAX, "%s/%s/block_%08llu",
                 SNAP_ROOT_DIR, snap_dir, (unsigned long long)block_num);

        blk_file = filp_open(block_path, O_RDONLY, 0);
        if (IS_ERR(blk_file)) {
            pr_err("%s: cannot open block file %s\n", MOD_NAME, block_path);
            ret = PTR_ERR(blk_file);
            goto out_close_dev;
        }

        /* Read block */
        if (kernel_read(blk_file, buf, dev.block_size, &pos) != dev.block_size) {
            pr_err("%s: failed to read block %llu\n", MOD_NAME, block_num);
            filp_close(blk_file, NULL);
            ret = -EIO;
            goto out_close_dev;
        }
        filp_close(blk_file, NULL);

        /* Write on the device */
        dev_pos = block_num * dev.block_size;
        if (kernel_write(dev_file, buf, dev.block_size, &dev_pos) != dev.block_size) {
            pr_err("%s: failed to write block %llu to device\n", MOD_NAME, block_num);
            ret = -EIO;
            goto out_close_dev;
        }
    }

out_close_dev:
    filp_close(dev_file, NULL);
out_unlock_metadata:
    mutex_unlock(&device_mutex);
out_free_metadata:
    snap_free_metadata(&dev);
out_free_heap:
    kfree(buf);
    kfree(block_path);
    kfree(snap_dir);
    kfree(dev_sanitized);

    return ret;
}

int restore_snapshot_for_device(const char *dev_name, const char *timestamp)
{
    return restore_snapshot_for_device_file(dev_name, timestamp);
}

