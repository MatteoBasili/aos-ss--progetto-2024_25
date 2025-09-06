#ifndef _BDEV_SNAPSHOT_UAPI_H
#define _BDEV_SNAPSHOT_UAPI_H

#include <linux/ioctl.h>

/* --- Costanti --- */
#define DEV_NAME_LEN       64
#define SNAP_PASSWORD_MAX  64

struct pw_args {
    char password[SNAP_PASSWORD_MAX];
};

/* --- Struct scambiata via ioctl --- */
struct snap_args {
    char dev_name[DEV_NAME_LEN];              /* Nome device, es. "loop0" */
    char password[SNAP_PASSWORD_MAX];    /* Password (opzionale per restore) */
};

/* --- IOCTL MAGIC --- */
#define SNAP_IOC_MAGIC    's'

/* --- IOCTL commands --- */
#define SNAP_ACTIVATE     _IOW(SNAP_IOC_MAGIC, 1, struct snap_args)
#define SNAP_DEACTIVATE   _IOW(SNAP_IOC_MAGIC, 2, struct snap_args)
#define SNAP_RESTORE      _IOW(SNAP_IOC_MAGIC, 3, struct snap_args)  /* Nuova ioctl per restore */
#define SNAP_SETPW        _IOW(SNAP_IOC_MAGIC, 4, struct pw_args)

#endif /* _BDEV_SNAPSHOT_UAPI_H */

