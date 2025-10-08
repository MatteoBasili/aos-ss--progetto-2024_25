#include <linux/blkdev.h>
#include <linux/namei.h>
#include <linux/version.h>

#include "snap_utils.h"

/* Simple string validation */
bool valid_string(const char *s, size_t len, size_t max_len)
{ 
    if (!s)
        return false;
        
    if (len == 0)
        return false;
        
    if (len >= max_len)
        return false;
        
    return true;
}

/* Validate device name string */
bool valid_dev_name(const char *dev_name, size_t max_len)
{
    size_t len;
    size_t i;

    if (!dev_name)
        return false;

    len = strnlen(dev_name, max_len);
    if (len == 0)
        return false;

    if (len >= max_len)
        return false;
        
    /* must start with '/' */
    if (dev_name[0] != '/')
        return false;    

    /* must not contain "//" */
    if (strstr(dev_name, "//"))
        return false;

    /* must not end with '/' */
    if (dev_name[len - 1] == '/')
        return false;

    /* valid characters: Printable ASCII except control characters */
    for (i = 0; i < len; i++) {
        unsigned char c = dev_name[i];

        if (c < 32 || c == 127)
            return false;
    }

    return true;
}

/* Validate snapshot password string */
bool valid_password(const char *passwd, size_t len, size_t max_len)
{
    bool has_alpha = false, has_digit = false;
    size_t i;

    if (!passwd)
        return false;

    if (len == 0)
        return false;

    if (len >= max_len)
        return false;

    if (len < 8)
        return false;

    for (i = 0; i < len; i++) {
        unsigned char c = passwd[i];

        if (c < 33 || c > 126)  /* prohibits spaces and control characters */
            return false;

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            has_alpha = true;
        else if (c >= '0' && c <= '9')
            has_digit = true;
    }

    if (!has_alpha || !has_digit)
        return false;

    return true;
}

/* Get block device name (with partition if applicable) */
void get_bdev_name(struct block_device *bdev, char *buf, size_t buf_size)
{
    unsigned int part_no;

    if (!bdev || !buf)
        return;

    part_no = MINOR(bdev->bd_dev) - bdev->bd_disk->first_minor;

    if (part_no == 0)
        snprintf(buf, buf_size, DEV_PREFIX "%s", bdev->bd_disk->disk_name); // disco intero
    else
        snprintf(buf, buf_size, DEV_PREFIX "%s%d", bdev->bd_disk->disk_name, part_no); // partizione
}

/* Retrieve backing file path for a loop device */
int get_loop_backing_path(struct block_device *bdev, char *buf, size_t buf_size)
{
    struct loop_device_meta *meta;
    struct file *backing_file;
    struct path path;
    char *tmp_buf = NULL;
    char *res;
    int ret;

    if (!bdev || !buf || buf_size == 0)
        return -EINVAL;

    meta = (struct loop_device_meta *)bdev->bd_disk->private_data;
    if (!meta || !meta->lo_backing_file)
        return -ENOENT;

    backing_file = meta->lo_backing_file;
    if (!backing_file)
        return -ENOENT;

    if (!backing_file->f_path.dentry || !backing_file->f_path.mnt)
        return -ENOENT;

    path = backing_file->f_path;
    path_get(&path);

    tmp_buf = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!tmp_buf) {
        path_put(&path);
        return -ENOMEM;
    }

    res = d_path(&path, tmp_buf, PATH_MAX);
    path_put(&path);

    if (IS_ERR(res)) {
        kfree(tmp_buf);
        return PTR_ERR(res);
    }

    ret = strscpy(buf, res, buf_size);
    kfree(tmp_buf);

    if (ret < 0)
        return -ENAMETOOLONG;

    return 0;
}

/* Get filesystem block size from inode */
size_t snap_get_filesystem_block_size_from_inode(struct inode *inode)
{
    if (!inode)
        return 0;

    /*
     * Preferiamo i_blkbits (block size effettiva in bit).
     * Se non disponibile o zero, usiamo s_blocksize come fallback.
     */
    if (inode->i_blkbits)
        return (1UL << inode->i_blkbits);

    if (inode->i_sb && inode->i_sb->s_blocksize)
        return inode->i_sb->s_blocksize;

    return 0;
}

/* Replace '/' with '_' for safe filenames */
void sanitize_devname(const char *in, char *out, size_t outlen)
{
    size_t i;
    for (i = 0; i < outlen - 1 && in[i]; i++)
        out[i] = (in[i] == '/') ? '_' : in[i];
    out[i] = '\0';
}

/* Ensure directory exists; create if missing (mode 0700) */
int ensure_dir(const char *path)
{
    struct path p;
    int err;
    
    err = kern_path(path, LOOKUP_DIRECTORY, &p);
    if (!err) {
	path_put(&p);
	return 0;
    }

    {
        struct path parent;
        struct dentry *dentry;
        umode_t mode = 0600;

        dentry = kern_path_create(AT_FDCWD, path, &parent, 0);
        if (IS_ERR(dentry))
            return PTR_ERR(dentry);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
        err = vfs_mkdir(mnt_idmap(parent.mnt),
                        d_inode(parent.dentry),
                        dentry,
                        mode);
#else
        err = vfs_mkdir(d_inode(parent.dentry), dentry, mode);
#endif        
        
        done_path_create(&parent, dentry);
        path_put(&parent);

        if (err && err != -EEXIST)
            return err;
            
    }

    return 0;
}

/* Convert epoch timestamp to YYYY-MM-DD_HH-MM-SS string */
void snapshot_time_to_string(time64_t ts, char *buf, size_t buf_size)
{
    struct tm tm;

    /* Convert to local time using timezone offset */
    time64_to_tm(ts, -sys_tz.tz_minuteswest * 60, &tm);

    snprintf(buf, buf_size, "%04ld-%02d-%02d_%02d-%02d-%02d",
             tm.tm_year + 1900,
             tm.tm_mon + 1,
             tm.tm_mday,
             tm.tm_hour,
             tm.tm_min,
             tm.tm_sec);
}

/* Comparator for qsort_kernel: descending order */
int cmp_timestamps_desc(const void *a, const void *b)
{
    const char *ts1 = a;
    const char *ts2 = b;
    return -strcmp(ts1, ts2); // minus => descending order
}

