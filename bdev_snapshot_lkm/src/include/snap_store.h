#ifndef _SNAP_STORE_H
#define _SNAP_STORE_H

#include "bdev_list.h"

/* Magic/version info */
#define SNAP_MAGIC    0x534E4150  /* "SNAP" in ASCII */
#define SNAP_VERSION  1

/* Work structure for saving a single block */
struct snap_block_work {
    struct work_struct work;
    struct snap_device *dev;
    int block_num;
    char *data;
    size_t len;
};

struct snap_pending_block {
    struct snap_device *dev;
    int block_num;
    size_t len;
    void *data;
    struct snap_pending_block *next;
};

/* Atomically check and mark a block as saved */
bool snap_try_mark_block_saved(struct snap_device *dev, u64 block);

void snap_block_work_handler(struct work_struct *work);

/* Returns list of snap_block_work ready to schedule, NULL if nothing */
struct snap_pending_block *snap_prepare_singlefilefs_block_save(struct snap_device *dev,
                                                                struct inode *inode,
                                                                loff_t *off);
                              
int open_snapshot(struct snap_device *dev);
void close_snapshot(struct snap_device *dev);

#endif

