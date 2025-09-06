#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>

#include "bdev_snapshot.h"
#include "bdev_auth.h"
#include "bdev_list.h"

/* --- Helper --- */
static bool valid_dev_name(const char *dev_name)
{
    return dev_name && dev_name[0] != '\0';
}

static int check_dev_and_perm(const char *dev_name)
{
    int ret = check_permission();
    if (ret)
        return ret;

    if (!valid_dev_name(dev_name))
        return -EINVAL;

    return 0;
}

/* --- Snapshot operations --- */
int activate_snapshot(const char *dev_name, const char *password)
{
    int ret;
    size_t pwlen = strnlen(password, SNAP_PASSWORD_MAX);

    ret = check_dev_and_perm(dev_name);
    if (ret)
        return ret;

    if (!verify_snap_password(password, pwlen)) {
        pr_warn("%s: SNAP_ACTIVATE authentication failed for %s\n",
                MOD_NAME, dev_name);
        return -EACCES;
    }

    ret = add_snap_device(dev_name);
    if (ret == -EEXIST)
        pr_warn("%s: device %s already active\n", MOD_NAME, dev_name);
    else if (ret == 0)
        pr_info("%s: device %s added to active list\n", MOD_NAME, dev_name);

    return ret;
}

int deactivate_snapshot(const char *dev_name, const char *password)
{
    int ret;
    size_t pwlen = strnlen(password, SNAP_PASSWORD_MAX);

    ret = check_dev_and_perm(dev_name);
    if (ret)
        return ret;

    if (!verify_snap_password(password, pwlen)) {
        pr_warn("%s: SNAP_DEACTIVATE authentication failed for %s\n",
                MOD_NAME, dev_name);
        return -EACCES;
    }

    ret = remove_snap_device(dev_name);
    if (ret == -ENOENT)
        pr_warn("%s: device %s not found\n", MOD_NAME, dev_name);
    else if (ret == 0)
        pr_info("%s: device %s removed from list\n", MOD_NAME, dev_name);

    return ret;
}

/* --- File operations --- */
int bdev_open(struct inode *inode, struct file *file)
{
    pr_info("%s: device opened\n", MOD_NAME);
    return 0;
}

int bdevsnap_release(struct inode *inode, struct file *file)
{
    pr_info("%s: device closed\n", MOD_NAME);
    return 0;
}

/* --- IOCTL --- */
long bdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = -ENOTTY;

    switch (cmd) {
    case SNAP_ACTIVATE:
    case SNAP_DEACTIVATE: {
        struct snap_args args;

        if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
            return -EFAULT;
            
        args.dev_name[DEV_NAME_LEN_MAX - 1] = '\0';
        args.password[SNAP_PASSWORD_MAX - 1] = '\0';

        if (cmd == SNAP_ACTIVATE)
            ret = activate_snapshot(args.dev_name, args.password);
        else
            ret = deactivate_snapshot(args.dev_name, args.password);

        memzero_explicit(args.password, sizeof(args.password));
        break;
    }
    case SNAP_RESTORE: {
        struct snap_args args;

        if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
            return -EFAULT;

        args.dev_name[DEV_NAME_LEN_MAX - 1] = '\0';

        /* restore_loop_device(args.dev_name); // opzionale */
        break;
    }
    case SNAP_SETPW: {
        struct pw_args pw;

        ret = check_permission();
        if (ret)
            break;

        if (copy_from_user(&pw, (void __user *)arg, sizeof(pw))) {
            ret = -EFAULT;
            break;
        }

        ret = set_snap_password(pw.password, pw.password_len);
        if (!ret)
            pr_info("%s: snapshot password set\n", MOD_NAME);
        else
            pr_warn("%s: failed to set snapshot password (err=%d)\n", MOD_NAME, ret);

        memzero_explicit(pw.password, sizeof(pw.password));
        break;
    }
    default:
        ret = -ENOTTY;
        break;
    }

    return ret;
}

