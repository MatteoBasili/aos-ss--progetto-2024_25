/*
 * bdev_restore.c
 *
 * Ripristino snapshot su dispositivi loop.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/xarray.h>
#include <linux/loop.h>
#include <linux/namei.h>
#include <linux/errno.h>
#include <linux/ktime.h>

#include "bdev_snapshot.h"
#include "bdev_store.h"

#define BYTES_PER_SECTOR 512

/* Funzione helper per scrivere un singolo blocco sul device */
static int write_block(struct block_device *bdev, sector_t sector, const void *buf)
{
    struct bio *bio;
    struct page *page;
    int ret = 0;

    page = alloc_page(GFP_KERNEL);
    if (!page)
        return -ENOMEM;

    memcpy(page_address(page), buf, BYTES_PER_SECTOR);

    /* Allocazione bio corretta */
    bio = bio_alloc(bdev, 1, GFP_KERNEL);
    if (!bio) {
        __free_page(page);
        return -ENOMEM;
    }

    bio_set_dev(bio, bdev);
    bio->bi_iter.bi_sector = sector;
    bio->bi_opf = REQ_OP_WRITE | REQ_SYNC;

    if (bio_add_page(bio, page, BYTES_PER_SECTOR, 0) != BYTES_PER_SECTOR)
        ret = -EIO;
    else
        ret = submit_bio_wait(bio);

    bio_put(bio);
    __free_page(page);
    return ret;
}

/* Restore di un device loop */
int restore_loop_device(const char *loop_name)
{
    struct snap_device *dev;
    struct snap_store *store;
    unsigned long sector;
    unsigned char *buf = NULL;
    struct file *snap_file;
    char block_path[SNAP_DIR_MAX + 64];
    struct block_device *bdev;
    struct file *floop;
    void *entry;
    int ret = 0;

    if (!loop_name)
        return -EINVAL;

    /* Trova snapshot device */
    mutex_lock(&snap_dev_mutex);
    dev = find_snap_device(loop_name);
    mutex_unlock(&snap_dev_mutex);

    if (!dev)
        return -ENOENT;

    /* Ottieni store (directory snapshot) */
    store = snap_store_get_or_create(loop_name, dev->first_write_ns);
    if (!store)
        return -ENOENT;

    /* Apri device loop (/dev/loopX) */
    floop = filp_open(loop_name, O_RDWR | O_LARGEFILE, 0);
    if (IS_ERR(floop))
        return PTR_ERR(floop);

    bdev = I_BDEV(floop->f_inode);
    if (!bdev) {
        filp_close(floop, NULL);
        return -ENODEV;
    }

    if (MAJOR(bdev->bd_dev) != LOOP_MAJOR) {
        pr_warn("%s: restore only allowed on loop devices\n", MOD_NAME);
        filp_close(floop, NULL);
        return -EINVAL;
    }

    buf = kmalloc(BYTES_PER_SECTOR, GFP_KERNEL);
    if (!buf) {
        filp_close(floop, NULL);
        return -ENOMEM;
    }

    /* Itera sui settori salvati nello xarray */
    xa_for_each(&store->saved_map, sector, entry) {
        snprintf(block_path, sizeof(block_path), "%s/block_%lu.bin", store->dir, sector);

        snap_file = filp_open(block_path, O_RDONLY, 0);
        if (IS_ERR(snap_file)) {
            pr_warn("%s: cannot open snapshot block %s\n", MOD_NAME, block_path);
            continue;
        }

        ret = kernel_read(snap_file, buf, BYTES_PER_SECTOR, &snap_file->f_pos);
        filp_close(snap_file, NULL);
        if (ret != BYTES_PER_SECTOR) {
            pr_warn("%s: read failed for block %lu\n", MOD_NAME, sector);
            continue;
        }

        ret = write_block(bdev, sector, buf);
        if (ret)
            pr_warn("%s: write failed for block %lu\n", MOD_NAME, sector);
    }

    kfree(buf);
    filp_close(floop, NULL);

    pr_info("%s: restore of %s completed\n", MOD_NAME, loop_name);
    return 0;
}

