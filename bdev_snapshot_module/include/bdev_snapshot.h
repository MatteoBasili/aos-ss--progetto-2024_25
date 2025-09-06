#ifndef _BDEV_SNAPSHOT_H
#define _BDEV_SNAPSHOT_H

#include <linux/module.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h> /* u64 */

#include "bdev_auth.h"
#include "bdev_ops.h"
#include "bdev_list.h"
#include "bdev_store.h"
#include "mod_name.h"
#include "uapi/bdev_snapshot_uapi.h"

/* --- Lista dispositivi snapshot --- */
struct snap_device {
	char dev_name[DEV_NAME_LEN];
	bool started; /* true alla prima write intercettata */
	u64 first_write_ns; /* timestamp ns monotonic alla prima write */
	struct list_head list;
};

/* --- Lista dispositivi --- */
int add_snap_device(const char *dev_name);
int remove_snap_device(const char *dev_name);
void clear_snap_devices(void);

/* Activate/deactivate */
int activate_snapshot(const char *dev_name, const char *password);
int deactivate_snapshot(const char *dev_name, const char *password);

/* Device file ops */
long bdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int bdev_open(struct inode *inode, struct file *file);
int bdevsnap_release(struct inode *inode, struct file *file);

/* --- F6: kprobe interface --- */
/* inizializza/termina kprobe per intercettare scritture */
int bdev_kprobe_init(void);
void bdev_kprobe_exit(void);

/* helper per consultare e marcare device attivi */
bool snapdev_is_active_name(const char *disk_name);
void snapdev_mark_started(const char *disk_name, u64 t_ns);

#endif /* _BDEV_SNAPSHOT_H */

