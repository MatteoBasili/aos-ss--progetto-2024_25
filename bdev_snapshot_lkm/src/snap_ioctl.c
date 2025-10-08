#include <linux/module.h>

#include "bdev_list.h"
#include "snap_auth.h"
#include "snap_ioctl.h"
#include "snap_restore.h"
#include "snap_utils.h"

/* Validate device name and password string */
static int check_dev_and_pw(const char *dev_name, const char *pw, size_t *pwlen)
{
    if (!valid_dev_name(dev_name, DEV_NAME_LEN_MAX)) {
        pr_err("%s: invalid device name\n", MOD_NAME);
        return -EINVAL;
    }

    *pwlen = strnlen(pw, SNAP_PASSWORD_MAX);
    if (!valid_password(pw, *pwlen, SNAP_PASSWORD_MAX)) {
        pr_err(SNAPSHOT_INVALID_PASSWD_MSG, MOD_NAME, SNAP_PASSWORD_MAX - 1);
        return -EINVAL;
    }

    return 0;
}

/* --- Snapshot operations --- */
int activate_snapshot(const char *dev_name, const char *password)
{
    int ret;
    size_t pwlen;
    
    ret = check_dev_and_pw(dev_name, password, &pwlen);
    if (ret)
        return ret;

    if (!verify_snap_password(password, pwlen)) {
        pr_warn("%s: authentication failed for activation on device %s\n",
                MOD_NAME, dev_name);
        return -EACCES;
    }

    ret = add_or_enable_snap_device(dev_name);
    if (ret == 0) {
        pr_info("%s: snapshot activated for device %s\n", MOD_NAME, dev_name);
    } else if (ret == 1) {
        pr_info("%s: snapshot already active for device %s\n", MOD_NAME, dev_name);
    } else {
        pr_err("%s: failed to activate snapshot for device %s (err=%d)\n",
                MOD_NAME, dev_name, ret);
    }

    return ret;
}

int deactivate_snapshot(const char *dev_name, const char *password)
{
    int ret;
    size_t pwlen;
    
    ret = check_dev_and_pw(dev_name, password, &pwlen);
    if (ret)
        return ret;
    
    if (!verify_snap_password(password, pwlen)) {
        pr_warn("%s: authentication failed for deactivation on device %s\n",
                MOD_NAME, dev_name);
        return -EACCES;
    }

    ret = disable_snap_device(dev_name);
    if (ret == 0) {
        pr_info("%s: snapshot deactivated for device %s\n", MOD_NAME, dev_name);
    } else if (ret == 1) {
        pr_info("%s: snapshot already inactive for device %s\n", MOD_NAME, dev_name);
    } else if (ret == -ENOENT) {
        pr_warn("%s: device %s not found\n", MOD_NAME, dev_name);
    } else {
        pr_err("%s: failed to deactivate snapshot for device %s (err=%d)\n",
                MOD_NAME, dev_name, ret);
    }

    return ret;
}

int list_snapshots(struct snap_list_args *out_args)
{
    int ret;

    if (!valid_dev_name(out_args->dev_name, DEV_NAME_LEN_MAX)) {
        pr_err("%s: invalid device name\n", MOD_NAME);
        return -EINVAL;
    }
    
    memset(out_args->timestamps, 0, sizeof(out_args->timestamps));

    ret = list_snapshots_for_device(out_args->dev_name,
                                    out_args->timestamps,
                                    &out_args->count);

    if (ret == -ENOENT) {
        pr_info("%s: no snapshots available for device %s\n",
                MOD_NAME, out_args->dev_name);
        out_args->count = 0;
        return 0; /* not an error, just empty list */
    }

    if (ret != 0) {
        pr_err("%s: failed to list snapshots for device %s (err=%d)\n",
                MOD_NAME, out_args->dev_name, ret);
        return ret;
    }

    pr_info("%s: found %d snapshots for device %s\n",
            MOD_NAME, out_args->count, out_args->dev_name);

    return 0;
}

int restore_snapshot(const char *dev_name, const char *password, const char *timestamp)
{
    int ret;
    size_t pwlen;
    
    ret = check_dev_and_pw(dev_name, password, &pwlen);
    if (ret)
        return ret;
    
    if (!valid_string(timestamp, strnlen(timestamp, SNAP_TIMESTAMP_MAX), SNAP_TIMESTAMP_MAX)) {
        pr_err("%s: invalid snapshot timestamp\n", MOD_NAME);
        return -EINVAL;
    }
    
    if (!verify_snap_password(password, pwlen)) {
        pr_warn("%s: authentication failed for restore on device %s\n",
                MOD_NAME, dev_name);
        return -EACCES;
    }

    /* Performs the actual restore */
    ret = restore_snapshot_for_device(dev_name, timestamp);
    if (ret == 0) {
        pr_info("%s: restore completed for device %s snapshot %s\n",
                MOD_NAME, dev_name, timestamp);
    } else if (ret == -EBUSY) {
        pr_warn("%s: restore aborted on device %s: snapshot still in progress\n", MOD_NAME, dev_name);
    } else {
        pr_err("%s: restore failed for device %s snapshot %s (err=%d)\n",
                MOD_NAME, dev_name, timestamp, ret);
    }

    return ret;
}

int set_snapshot_pw(const char *password)
{
    int ret;
    size_t pwlen = strnlen(password, SNAP_PASSWORD_MAX);
    
    if (!valid_password(password, pwlen, SNAP_PASSWORD_MAX)) {
        pr_err(SNAPSHOT_INVALID_PASSWD_MSG, MOD_NAME, SNAP_PASSWORD_MAX - 1);
        return -EINVAL;
    }
    
    ret = set_snap_password(password, pwlen);
    if (!ret)
        pr_info("%s: snapshot password set\n", MOD_NAME);
    else
        pr_err("%s: failed to set snapshot password (err=%d)\n", MOD_NAME, ret);
        
    return ret;
}

/* --- File operations --- */
int snap_dev_open(struct inode *inode, struct file *file)
{
    /* Prevent module removal while device is open */
    if (!try_module_get(THIS_MODULE))
        return -ENODEV;

    pr_debug("%s: device opened\n", MOD_NAME);
    return 0;
}

int snap_dev_release(struct inode *inode, struct file *file)
{    
    module_put(THIS_MODULE);
    pr_debug("%s: device released\n", MOD_NAME);
    return 0;
}

/* --- IOCTL dispatcher --- */
long snap_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = -ENOTTY;

    switch (cmd) {
    case SNAP_ACTIVATE:
    case SNAP_DEACTIVATE: {
        struct snap_args *args;

        ret = check_permission();
        if (ret)
            break;

        args = memdup_user((const void __user *)arg, sizeof(*args));
        if (IS_ERR(args))
            return PTR_ERR(args);

        if (cmd == SNAP_ACTIVATE)
            ret = activate_snapshot(args->dev_name, args->password);
        else
            ret = deactivate_snapshot(args->dev_name, args->password);

        memzero_explicit(args->password, sizeof(args->password));
        kfree(args);
        break;
    }
    case SNAP_LIST: {
        struct snap_list_args *args;

        ret = check_permission();
        if (ret)
            break;

        args = memdup_user((const void __user *)arg, sizeof(*args));
        if (IS_ERR(args))
            return PTR_ERR(args);

        ret = list_snapshots(args);

        if (ret == 0) {
            if (copy_to_user((void __user *)arg, args, sizeof(*args)))
                ret = -EFAULT;
        }

        kfree(args);
        break;
    }
    case SNAP_RESTORE: {
        struct snap_restore_args *args;

        ret = check_permission();
        if (ret)
            break;

        args = memdup_user((const void __user *)arg, sizeof(*args));
        if (IS_ERR(args))
            return PTR_ERR(args);
        
        ret = restore_snapshot(args->dev_name, args->password, args->timestamp);

        memzero_explicit(args->password, sizeof(args->password));
        kfree(args);
        break;
    }
    case SNAP_SETPW: {
        struct pw_arg *pwarg;

        ret = check_permission();
        if (ret)
            break;

        pwarg = memdup_user((const void __user *)arg, sizeof(*pwarg));
        if (IS_ERR(pwarg))
            return PTR_ERR(pwarg);

        ret = set_snapshot_pw(pwarg->password);

        memzero_explicit(pwarg->password, sizeof(pwarg->password));
        kfree(pwarg);
        break;
    }
    default:
        pr_warn("%s: unknown ioctl command %u\n", MOD_NAME, cmd);
        break;
    }

    return ret;
}

