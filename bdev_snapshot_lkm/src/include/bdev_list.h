#ifndef _BDEV_LIST_H
#define _BDEV_LIST_H

#include "uapi/bdev_snapshot.h"

/* Snapshot device representation */
struct snap_device {
    char dev_name[DEV_NAME_LEN_MAX];
    bool enabled;                  /* true = snapshot active */
    bool mounted;                  /* true = scurrently mounted and snapshot in progress */
    struct timespec64 mount_time;  /* mount timestamp */
    struct list_head list;         
    struct kref ref;               
    struct mutex lock;
    spinlock_t spin_lock;             
    unsigned long *saved_bitmap;   /* bitmap for saved blocks */
    unsigned long num_blocks;      /* number of blocks in the device */
    u64 block_size;                /* actual block size of the device (filesystem block size) */
    loff_t device_size;
    char snapshot_dir[DEV_NAME_LEN_MAX + 32]; /* folder name for current snapshot */
    struct workqueue_struct *wq;
};

int add_or_enable_snap_device(const char *dev_name);
int disable_snap_device(const char *dev_name);
void clear_snap_devices(void);

struct snap_device *snap_find_device_get(const char *dev_name);
void snap_device_get(struct snap_device *dev);
void snap_device_put(struct snap_device *dev);

int snapdev_mark_mounted(struct snap_device *dev);
int snapdev_do_mount_work(struct snap_device *dev);
int snapdev_mark_unmounted(struct snap_device *dev);
int snapdev_do_unmount_work(struct snap_device *dev);

struct file *snapdev_get_backing_file(struct snap_device *dev);
struct file *snapdev_get_snapshot_file(struct snap_device *dev);
void snapdev_put_file(struct file *filp);
unsigned long *snapdev_get_saved_bitmap(struct snap_device *dev);
bool snapdev_is_mounted(struct snap_device *dev);

#endif

