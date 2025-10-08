#ifndef _SNAP_UTILS_H
#define _SNAP_UTILS_H

#include <linux/loop.h>

#define DEV_PREFIX "/dev/"

#define SNAPSHOT_INVALID_PASSWD_MSG \
    "%s: invalid password — must be 8–%d characters long, contain at least one letter and one digit, " \
    "and only use printable non-space ASCII characters (33–126)\n"

/* Metadata for loop devices */
struct loop_device_meta {
      int lo_number;
      loff_t lo_offset;
      loff_t lo_sizelimit;
      int lo_flags;
      char lo_file_name[LO_NAME_SIZE];

      struct file *lo_backing_file;
};

/* Simple string validation */
bool valid_string(const char *s, size_t len, size_t max_len);

/* Validate device name string */
bool valid_dev_name(const char *dev_name, size_t max_len);

/* Validate snapshot password string */
bool valid_password(const char *passwd, size_t len, size_t max_len);

/* Get device name for a block device (with partition number) */
void get_bdev_name(struct block_device *bdev, char *buf, size_t buf_size);

/* Get path of backing file for loop device */
int get_loop_backing_path(struct block_device *bdev, char *buf, size_t buf_size);

/* Get filesystem block size from inode */
size_t snap_get_filesystem_block_size_from_inode(struct inode *inode);

/* Sanitize string for safe filesystem use (replace '/' with '_') */
void sanitize_devname(const char *in, char *out, size_t outlen);

/* Ensure directory exists, create if necessary (mode 0700) */
int ensure_dir(const char *path);

/* Convert timestamp to human-readable string (YYYY-MM-DD_HH-MM-SS) */
void snapshot_time_to_string(time64_t ts, char *buf, size_t buf_size);

/* Comparator for qsort_kernel: descending order of timestamps */
int cmp_timestamps_desc(const void *a, const void *b);

#endif

