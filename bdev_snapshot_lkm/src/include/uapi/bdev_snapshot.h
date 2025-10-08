#ifndef _BDEV_SNAPSHOT_H
#define _BDEV_SNAPSHOT_H

/* -------------------------------------------------------------------
 * Device file configuration
 * ------------------------------------------------------------------- */
#define SNAP_DEVICE_FILE   "bdev_snapshot_ctrl"
#define SNAP_DEVICE_PATH   "/dev/" SNAP_DEVICE_FILE

/* -------------------------------------------------------------------
 * Limits and constants
 * ------------------------------------------------------------------- */
#define DEV_NAME_LEN_MAX   513   /* Maximum device name length */
#define SNAP_PASSWORD_MAX  65   /* Maximum password length */
#define SNAP_ROOT_DIR      "/snapshot" /* Root directory for stored snapshots */

#define MAX_SNAPSHOTS      32    /* Maximum snapshots per device */
#define SNAP_TIMESTAMP_MAX 20    /* Maximum length of timestamp string */

#define MOD_NAME "bdev_snapshot"

/* -------------------------------------------------------------------
 * Structures exchanged with ioctl()
 * ------------------------------------------------------------------- */

/**
 * struct snap_list_args - Used with SNAP_LIST
 * @dev_name:    Input device name
 * @timestamps:  Output array of snapshot timestamps
 * @count:       Output number of snapshots found
 */
struct snap_list_args {
    char dev_name[DEV_NAME_LEN_MAX];
    char timestamps[MAX_SNAPSHOTS][SNAP_TIMESTAMP_MAX];
    int count;
};

/**
 * struct snap_args - Used with SNAP_ACTIVATE / SNAP_DEACTIVATE
 * @dev_name:  Device name
 * @password:  Password to use the service
 */
struct snap_args {
    char dev_name[DEV_NAME_LEN_MAX];
    char password[SNAP_PASSWORD_MAX];
};

/**
 * struct snap_restore_args - Used with SNAP_RESTORE
 * @dev_name:   Device name to restore
 * @password:   Password to use the service
 * @timestamp:  Timestamp of the snapshot to restore
 */
struct snap_restore_args {
    char dev_name[DEV_NAME_LEN_MAX];
    char password[SNAP_PASSWORD_MAX];
    char timestamp[SNAP_TIMESTAMP_MAX];
};

/**
 * struct pw_arg - Used with SNAP_SETPW
 * @password:  New password to configure
 */
struct pw_arg {
    char password[SNAP_PASSWORD_MAX];
};

/* -------------------------------------------------------------------
 * IOCTL interface
 * ------------------------------------------------------------------- */
#define SNAP_IOC_MAGIC    's'

/* Ioctl commands */
#define SNAP_LIST         _IOW(SNAP_IOC_MAGIC, 1, struct snap_list_args)
#define SNAP_ACTIVATE     _IOW(SNAP_IOC_MAGIC, 2, struct snap_args)
#define SNAP_DEACTIVATE   _IOW(SNAP_IOC_MAGIC, 3, struct snap_args)
#define SNAP_RESTORE      _IOW(SNAP_IOC_MAGIC, 4, struct snap_restore_args)
#define SNAP_SETPW        _IOW(SNAP_IOC_MAGIC, 5, struct pw_arg)

#endif

