#ifndef _SNAP_RESTORE_H
#define _SNAP_RESTORE_H

#include "uapi/bdev_snapshot.h"

/* -------------------------------------------------------------------
 * Data structures
 * ------------------------------------------------------------------- */

/* Context for snapshot enumeration */
struct snap_list_ctx {
    struct dir_context ctx;
    char (*timestamps)[SNAP_TIMESTAMP_MAX]; /* output timestamp array */
    int count;                              /* number of snapshots found */
    char dev_sanitized[DEV_NAME_LEN_MAX];   /* sanitized device name */
};

struct snap_restore_tmp {
    u64 block_size;
    u64 num_blocks;
    u64 *saved_blocks;       // array dei blocchi salvati
    int num_saved_blocks;    // numero di blocchi salvati
};

/**
 * list_snapshots_for_device - Enumerate all snapshots available for a device
 * @dev_name:     Device name
 * @timestamps:   Output array of timestamp strings
 * @count:        Output number of snapshots found
 *
 * Return: 0 on success, negative error code on failure.
 */
int list_snapshots_for_device(const char *dev_name,
                              char timestamps[MAX_SNAPSHOTS][SNAP_TIMESTAMP_MAX],
                              int *count);

int snap_load_metadata(struct snap_restore_tmp *dev, const char *snap_dir);
void snap_free_metadata(struct snap_restore_tmp *dev);

/**
 * restore_snapshot_for_device - Restore a device from a snapshot
 * @dev_name:   Target device name
 * @timestamp:  Snapshot timestamp to restore
 *
 * Return: 0 on success, negative error code on failure.
 */
int restore_snapshot_for_device(const char *dev_name, const char *timestamp);

#endif

