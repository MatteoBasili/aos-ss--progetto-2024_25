#ifndef _BDEV_SNAPSHOT_UAPI_H
#define _BDEV_SNAPSHOT_UAPI_H

#include <linux/ioctl.h>

/* --- IOCTL MAGIC --- */
#define SNAP_IOC_MAGIC  's'
#define SNAP_ACTIVATE   _IOW(SNAP_IOC_MAGIC, 1, struct snap_args)
#define SNAP_DEACTIVATE _IOW(SNAP_IOC_MAGIC, 2, struct snap_args)

/* --- Costanti --- */
#define DEV_NAME_LEN       64
#define SNAP_PASSWORD_MAX  63

/* --- Struct scambiata via ioctl --- */
struct snap_args {
    char dev_name[DEV_NAME_LEN];
    char password[SNAP_PASSWORD_MAX + 1];
};

#endif

