#ifndef _BDEV_SNAPSHOT_H
#define _BDEV_SNAPSHOT_H

#include <linux/list.h>
#include <linux/mutex.h>
#include "include/uapi/bdev_snapshot_uapi.h"

#define MOD_NAME "bdev_snapshot"
#define PW_HASH_LEN 32   /* SHA-256 */

/* --- Lista dispositivi snapshot --- */
struct snap_device {
    char dev_name[DEV_NAME_LEN];
    struct list_head list;
};

/* --- Funzioni globali --- */
int  set_password_from_user(const char *pw, size_t pwlen);
bool verify_password_from_user(const char *pw, size_t pwlen);
void clear_password(void);

int  add_snap_device(const char *dev_name);
int  remove_snap_device(const char *dev_name);
void clear_snap_devices(void);

int  activate_snapshot(const char *dev_name, const char *password);
int  deactivate_snapshot(const char *dev_name, const char *password);

long bdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int  bdev_open(struct inode *inode, struct file *file);
int  bdev_release(struct inode *inode, struct file *file);

#endif

