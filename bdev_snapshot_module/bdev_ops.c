#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/capability.h>

#include "bdev_snapshot.h"
#include "bdev_password.h"

/* --- Activate/deactivate --- */
int activate_snapshot(const char *dev_name, const char *password)
{
    int ret;
    size_t pwlen = strnlen(password, SNAP_PASSWORD_MAX);

    if (!is_password_set()) {
        /* Prima impostazione */
        ret = set_password_from_user(password, pwlen);
        if (ret)
            return ret;
        pr_info("%s: password set (first-time). Snapshot ACTIVATED on %s\n",
                MOD_NAME, dev_name);
    } else {
        /* Verifica password se già impostata */
        if (!verify_password_from_user(password, pwlen)) {
            pr_warn("%s: SNAP_ACTIVATE authentication failed for %s\n",
                    MOD_NAME, dev_name);
            return -EACCES;
        }
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

    if (!verify_password_from_user(password, pwlen)) {
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

long bdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct snap_args args;
    int ret;

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
        return -EFAULT;

    args.dev_name[sizeof(args.dev_name)-1] = '\0';
    args.password[sizeof(args.password)-1] = '\0';

    switch (cmd) {
    case SNAP_ACTIVATE:
        ret = activate_snapshot(args.dev_name, args.password);
        break;
    case SNAP_DEACTIVATE:
        ret = deactivate_snapshot(args.dev_name, args.password);
        break;
    default:
        ret = -ENOTTY;
        break;
    }

    memzero_explicit(args.password, sizeof(args.password));
    return ret;
}

