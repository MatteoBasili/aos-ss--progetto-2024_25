#ifndef BDEV_OPS_H
#define BDEV_OPS_H

/* --- Snapshot operations --- */
int activate_snapshot(const char *dev_name, const char *password);
int deactivate_snapshot(const char *dev_name, const char *password);

/* --- File operations --- */
int bdev_open(struct inode *inode, struct file *file);
int bdevsnap_release(struct inode *inode, struct file *file);

/* --- IOCTL --- */
long bdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif /* BDEV_OPS_H */

