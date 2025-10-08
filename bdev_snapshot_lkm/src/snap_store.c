#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/crc32.h>

#include "snap_store.h"
#include "snap_utils.h"
#include "uapi/bdev_snapshot.h"

static int snap_save_block_to_file(struct snap_device *dev, u64 block_num, void *data, size_t len)
{
    char *path;
    struct file *filp;
    loff_t pos = 0;
    int ret;

    if (!dev || !data)
        return -EINVAL;

    path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!path)
        return -ENOMEM;

    scnprintf(path, PATH_MAX, "%s/%s/block_%08llu",
              SNAP_ROOT_DIR, dev->snapshot_dir, (unsigned long long)block_num);

    filp = filp_open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    kfree(path);
    if (IS_ERR(filp))
        return PTR_ERR(filp);

    ret = kernel_write(filp, data, len, &pos);
    if (ret < 0)
        pr_warn("%s: failed to write block %llu\n", MOD_NAME, (unsigned long long)block_num);

    filp_close(filp, NULL);
    return ret;
}

/* -------------------------------------------------------------------
 * Aggiorna metadata.json aggiungendo un nuovo blocco
 * ------------------------------------------------------------------- */
static int snap_update_metadata_block(struct snap_device *dev, u64 block_num)
{
    char *path, *buf, *new_buf;
    struct file *filp;
    struct inode *inode;
    loff_t size;
    loff_t pos = 0;
    int ret = 0;
    char *p, *end;
    int new_len;

    if (!dev)
        return -EINVAL;

    path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!path)
        return -ENOMEM;

    scnprintf(path, PATH_MAX, "%s/%s/metadata.json",
              SNAP_ROOT_DIR, dev->snapshot_dir);

    filp = filp_open(path, O_RDWR, 0);
    kfree(path);
    if (IS_ERR(filp)) {
        ret = PTR_ERR(filp);
        goto out;
    }

    inode = file_inode(filp);
    size  = i_size_read(inode);
    if (size <= 0) {
        ret = -EINVAL;
        goto out_close;
    }

    buf = kmalloc(size + 1, GFP_KERNEL);
    if (!buf) {
        ret = -ENOMEM;
        goto out_close;
    }

    /* leggi tutto il file */
    ret = kernel_read(filp, buf, size, &pos);
    if (ret < 0) {
        kfree(buf);
        goto out_close;
    }
    buf[size] = '\0';

    /* trova "blocks": [ */
    p = strnstr(buf, "\"blocks\": [", size);
    if (!p) {
        pr_err("%s: metadata.json formato inatteso (manca blocks)\n", MOD_NAME);
        ret = -EINVAL;
        kfree(buf);
        goto out_close;
    }

    /* trova la ']' corrispondente */
    end = strchr(p, ']');
    if (!end) {
        pr_err("%s: metadata.json formato inatteso (manca ])\n", MOD_NAME);
        ret = -EINVAL;
        kfree(buf);
        goto out_close;
    }

    /* costruiamo nuova stringa con il blocco */
    new_buf = kmalloc(size + 32, GFP_KERNEL); /* spazio extra per numero */
    if (!new_buf) {
        ret = -ENOMEM;
        kfree(buf);
        goto out_close;
    }

    /* copia parte fino a prima di ']' */
    new_len = end - buf;
    memcpy(new_buf, buf, new_len);

    /* decidiamo se è il primo elemento o no */
    if (*(end - 1) == '[') {
        new_len += scnprintf(new_buf + new_len,
                             32, "%llu", (unsigned long long)block_num);
    } else {
        new_len += scnprintf(new_buf + new_len,
                             32, ", %llu", (unsigned long long)block_num);
    }

    /* copia il resto compreso da ']' in poi */
    strcpy(new_buf + new_len, end);

    /* riscrivi tutto il file */
    pos = 0;
    ret = kernel_write(filp, new_buf, strlen(new_buf), &pos);

    kfree(buf);
    kfree(new_buf);

out_close:
    filp_close(filp, NULL);
out:
    return ret;
}


static int mark_snapshot_closed(struct snap_device *dev)
{
    char *path, *buf;
    struct file *filp;
    struct inode *inode;
    loff_t size;
    loff_t pos = 0;
    int ret = 0;

    if (!dev)
        return -EINVAL;

    path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!path)
        return -ENOMEM;

    scnprintf(path, PATH_MAX, "%s/%s/metadata.json",
              SNAP_ROOT_DIR, dev->snapshot_dir);

    filp = filp_open(path, O_RDWR, 0);
    kfree(path);
    if (IS_ERR(filp))
        return PTR_ERR(filp);

    inode = file_inode(filp);
    size  = i_size_read(inode);

    buf = kmalloc(size + 1, GFP_KERNEL);
    if (!buf) {
        filp_close(filp, NULL);
        return -ENOMEM;
    }

    /* leggi tutto il file in memoria */
    ret = kernel_read(filp, buf, size, &pos);
    if (ret < 0) {
        kfree(buf);
        filp_close(filp, NULL);
        return ret;
    }
    buf[size] = '\0';

    /* cerca la stringa "open": 1 */
    {
        char *p = strnstr(buf, "\"open\": 1", size);
        if (p) {
            p[8] = '0';  // sostituisci il carattere '1' con '0'
        } else {
            pr_warn("%s: campo 'open' non trovato in metadata.json\n", MOD_NAME);
        }
    }

    /* riscrivi l’intero file aggiornato */
    pos = 0;
    ret = kernel_write(filp, buf, size, &pos);

    kfree(buf);
    filp_close(filp, NULL);
    return ret;
}

/* -------------------------------------------------------------------
 * Atomically check and mark a block as saved
 * Returns true if block was already saved, false if it was just marked
 * ------------------------------------------------------------------- */
bool snap_try_mark_block_saved(struct snap_device *dev, u64 block)
{
    unsigned long *bitmap;

    if (!dev)
        return true; /* treat invalid dev as "already saved" */

    bitmap = snapdev_get_saved_bitmap(dev);
    if (!bitmap)
        return true; /* treat invalid bitmap as already saved */

    /* test_and_set_bit returns previous value: true = already set */
    return test_and_set_bit(block, bitmap);
}

/* -------------------------------------------------------------------
 * Workqueue handler: save a single block into snapshot
 * ------------------------------------------------------------------- */
void snap_block_work_handler(struct work_struct *work)
{
    struct snap_block_work *bw = container_of(work, struct snap_block_work, work);
    struct snap_device *dev;

    if (!bw || !bw->dev || !bw->data)
        goto out_free;

    dev = bw->dev;

    // Save block to separate file
    snap_save_block_to_file(dev, bw->block_num, bw->data, bw->len);

    // Update metadata.json
    snap_update_metadata_block(dev, bw->block_num);

out_free:
    if (bw->data)
        kfree(bw->data);
    snap_device_put(dev);
    kfree(bw);
}

/* Restituisce lista di snap_block_work pronta da schedulare, NULL se nulla */
struct snap_pending_block *snap_prepare_block_save(struct snap_device *dev, struct inode *inode, loff_t *off)
{
    size_t block_size;
    struct buffer_head *bh;
    int block_nr;
    struct snap_pending_block *blk, *first = NULL, *last = NULL;

    if (!dev || !inode || !inode->i_sb || !off) {
        return NULL;
    }

    block_size = snap_get_filesystem_block_size_from_inode(inode);
    if (block_size == 0) {
        return NULL;
    }    
        
    block_nr = *off / block_size + 2;
    
    bh = sb_bread(inode->i_sb, block_nr);
    if (bh) {
        blk = kmalloc(sizeof(*blk), GFP_ATOMIC);
        if (blk) {
            blk->data = kmemdup(bh->b_data, block_size, GFP_ATOMIC);
            if (blk->data) {
                blk->dev = dev;
                blk->block_num = block_nr;
                blk->len = block_size;
                blk->next = NULL;

                if (!first) first = blk;
                if (last) last->next = blk;
                last = blk;                                
            } else {                
                kfree(blk);
            }
        }
        brelse(bh);
    }   
    
    block_nr = 1; // of the inode    
    
    bh = sb_bread(inode->i_sb, block_nr);
    if (bh) {
        blk = kmalloc(sizeof(*blk), GFP_ATOMIC);
        if (blk) {
            blk->data = kmemdup(bh->b_data, block_size, GFP_ATOMIC);
            if (blk->data) {
                blk->dev       = dev;
                blk->block_num = block_nr;
                blk->len       = block_size;
                blk->next = NULL;

                if (!first) first = blk;
                if (last) last->next = blk;
                last = blk;                                
            } else {                
                kfree(blk);
            }
        }
        brelse(bh);
    }           
    
    return first;
}

/* Create snapshot directory under /snapshot with device name and timestamp */
static int create_snapshot_dir(const char *dir_name)
{
    char *full_path;

    if (!dir_name || !*dir_name)
        return -EINVAL;

    if (ensure_dir(SNAP_ROOT_DIR) != 0) {
        pr_warn("%s: failed to create snapshot root directory\n", MOD_NAME);
        return -EIO;
    }

    full_path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!full_path) {
        pr_warn("%s: failed to allocate memory for snapshot dirpath\n", MOD_NAME);
        return -ENOMEM;
    }

    scnprintf(full_path, PATH_MAX, "%s/%s", SNAP_ROOT_DIR, dir_name);

    if (ensure_dir(full_path) == 0) {
        pr_info("%s: mount detected, snapshot directory created (%s)\n", MOD_NAME, full_path);
        kfree(full_path);
        return 0;
    } else {
        pr_err("%s: mount detected, failed to create snapshot directory (%s)\n", MOD_NAME, full_path);
        kfree(full_path);
        return -EIO;
    }
}

/* -------------------------------------------------------------------
 * Initialize snapshot: create metadata.json inside the snapshot dir
 * ------------------------------------------------------------------- */
static int initialize_snapshot(struct snap_device *dev, const char *dir_name)
{
    char *path, *json_buf;
    struct file *filp;
    loff_t pos = 0;
    int ret;

    if (!dev || !dir_name)
        return -EINVAL;

    path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!path)
        return -ENOMEM;

    path     = kmalloc(PATH_MAX, GFP_KERNEL);
    json_buf = kmalloc(1024, GFP_KERNEL);
    if (!path || !json_buf) {
        kfree(path);
        kfree(json_buf);
        return -ENOMEM;
    }

    scnprintf(path, PATH_MAX, "%s/%s/metadata.json", SNAP_ROOT_DIR, dir_name);

    filp = filp_open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    kfree(path);

    if (IS_ERR(filp)) {
        pr_err("%s: cannot create metadata.json for %s\n", MOD_NAME, dev->dev_name);
        kfree(json_buf);
        return PTR_ERR(filp);
    }

    /* Write initial JSON */
    ret = scnprintf(json_buf, 1024,
        "{\n"
        "  \"device_name\": \"%s\",\n"
        "  \"snapshot_id\": \"%s\",\n"
        "  \"timestamp\": \"%llu\",\n"
        "  \"block_size\": %llu,\n"
        "  \"device_size\": %llu,\n"
        "  \"num_blocks\": %llu,\n"
        "  \"open\": 1,\n"
        "  \"blocks\": []\n"
        "}\n",
        dev->dev_name,
        dir_name,
        (unsigned long long)dev->mount_time.tv_sec,
        (unsigned long long)dev->block_size,
        (unsigned long long)dev->device_size,
        (unsigned long long)dev->num_blocks
    );

    kernel_write(filp, json_buf, ret, &pos);
    filp_close(filp, NULL);
    kfree(json_buf);

    pr_info("%s: metadata.json initialized for %s\n", MOD_NAME, dev->dev_name);
    return 0;
}

/* -------------------------------------------------------------------
 * Create/open snapshot directory and metadata.json
 * ------------------------------------------------------------------- */
int open_snapshot(struct snap_device *dev)
{
    char tsbuf[32];
    struct inode *inode;
    u64 block_size;
    loff_t dev_size;
    struct file *backing_filp;

    if (!dev)
        return -EINVAL;

    /* Open backing device file */
    backing_filp = filp_open(dev->dev_name, O_RDONLY | O_LARGEFILE, 0);
    if (IS_ERR(backing_filp)) {
        pr_err("%s: cannot open backing device %s\n", MOD_NAME, dev->dev_name);
        return PTR_ERR(backing_filp);
    }

    inode = backing_filp->f_inode;
    if (!inode) {
        filp_close(backing_filp, NULL);
        return -EINVAL;
    }

    /* Determine actual block_size from filesystem */
    block_size = snap_get_filesystem_block_size_from_inode(inode);
    if (block_size == 0) {
        filp_close(backing_filp, NULL);
        return -EINVAL;
    }
    dev->block_size = block_size;

    /* Compute device size and number of blocks */
    dev_size = i_size_read(inode);
    dev->device_size = dev_size;
    dev->num_blocks = (dev_size + block_size - 1) / block_size;
    
    filp_close(backing_filp, NULL);

    /* Create folder name: <devname>_<timestamp> */
    sanitize_devname(dev->dev_name, dev->snapshot_dir, sizeof(dev->snapshot_dir));
    snapshot_time_to_string(dev->mount_time.tv_sec, tsbuf, sizeof(tsbuf));
    strlcat(dev->snapshot_dir, "_", sizeof(dev->snapshot_dir));
    strlcat(dev->snapshot_dir, tsbuf, sizeof(dev->snapshot_dir));

    /* Create snapshot directory */
    if (create_snapshot_dir(dev->snapshot_dir) != 0) {
        pr_err("%s: failed to create snapshot directory for %s\n", MOD_NAME, dev->dev_name);
        return -EIO;
    }

    /* Initialize metadata.json */
    return initialize_snapshot(dev, dev->snapshot_dir);
}

/* -------------------------------------------------------------------
 * Close snapshot file
 * ------------------------------------------------------------------- */
void close_snapshot(struct snap_device *dev)
{   
    if (!dev)
        return;
        
    mark_snapshot_closed(dev);
    
    pr_debug("%s: snapshot file closed for %s\n", MOD_NAME, dev->dev_name);
}

