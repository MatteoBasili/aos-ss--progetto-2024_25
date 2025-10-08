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

/* Global metadata (at beginning of snapshot file) */
struct snap_metadata {
    __le32 magic;       /* must be SNAP_MAGIC */
    __le16 version;     /* snapshot format version */
    __le16 flags;       /* reserved/feature flags */
    __le64 block_size;  /* snapshot block size */
    __le64 num_blocks;  /* total blocks in device */
    __u8   open;        /* 1 = open, 0 = closed */
    __u8   reserved[7]; /* alignment */
};


/* Per-block header */
struct snap_block_header {
    __le64 block_num;   /* block number */
    __le32 len;         /* length (<= block_size) */
    __le32 checksum;    /* CRC32 of data */
};

/* -------------------------------------------------------------------
 * Atomically check and mark a block as saved
 * Returns true if block was already saved, false if it was just marked
 * ------------------------------------------------------------------- */
bool snap_try_mark_block_saved(struct snap_device *dev, u64 block);

void snap_block_work_handler(struct work_struct *work);

/* Restituisce lista di snap_block_work pronta da schedulare, NULL se nulla */
struct snap_pending_block *snap_prepare_block_save(struct snap_device *dev, struct inode *inode, loff_t *off);
                              
/* Open / close snapshot file (single file per device) */
int open_snapshot(struct snap_device *dev);
void close_snapshot(struct snap_device *dev);

#endif

