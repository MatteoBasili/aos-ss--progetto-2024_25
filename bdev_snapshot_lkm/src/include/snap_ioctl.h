#ifndef _SNAP_IOCTL_H
#define _SNAP_IOCTL_H

#include "uapi/bdev_snapshot.h"

/* --- Snapshot operations --- */
int activate_snapshot(const char *dev_name, const char *password);
int deactivate_snapshot(const char *dev_name, const char *password);
int list_snapshots(struct snap_list_args *out_args);
int restore_snapshot(const char *dev_name, const char *password, const char *timestamp);
int set_snapshot_pw(const char *password);

/* --- File operations --- */
int snap_dev_open(struct inode *inode, struct file *file);
int snap_dev_release(struct inode *inode, struct file *file);

/* --- IOCTL handler --- */
long snap_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif

