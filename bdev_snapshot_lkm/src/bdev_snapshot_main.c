#include <linux/module.h>

#include "bdev_kprobe.h"
#include "bdev_list.h"
#include "cdev_snap.h"
#include "snap_auth.h"
#include "snap_ioctl.h"
#include "uapi/bdev_snapshot.h"

/* Module parameter: initial password */
static char snap_passwd[SNAP_PASSWORD_MAX];
module_param_string(snap_passwd, snap_passwd, SNAP_PASSWORD_MAX, 0);
MODULE_PARM_DESC(snap_passwd, "Initial snapshot service password (plain text, hashed at load)");

static int __init bdevsnapshot_init(void)
{
    int ret;

    pr_info("%s: loading module...\n", MOD_NAME);

    /* --- Init auth subsystem --- */
    ret = bdev_auth_init();
    if (ret) {
	pr_err("%s: auth init failed (%d)\n", MOD_NAME, ret);
	return ret;
    }
    
    /* Handle initial password, if provided */
    if (snap_passwd[0] != '\0') {
        ret = set_snapshot_pw(snap_passwd);
        memzero_explicit(snap_passwd, SNAP_PASSWORD_MAX);
        if (ret) {
            bdev_auth_exit();
            return ret;
        }
    } else {
        pr_warn("%s: no initial password set, must configure later\n", MOD_NAME);
    }

    /* Init char device */
    ret = cdev_snap_init();
    if (ret) {
        pr_err("%s: char device init failed (%d)\n", MOD_NAME, ret);
        goto err_cdev_init;
    }
    
    /* Init kprobes */
    ret = bdev_kprobe_module_init();
    if (ret) {
        pr_err("%s: kprobe init failed (%d)\n", MOD_NAME, ret);
        goto err_kprobe_init;
    }

    pr_info("%s: module loaded successfully\n", MOD_NAME);
    return 0;

    /* --- Error paths --- */
err_kprobe_init:
    cdev_snap_exit();
err_cdev_init:
    bdev_auth_exit();    
    return ret;
}

static void __exit bdevsnapshot_exit(void)
{
    bdev_kprobe_module_exit();
    clear_snap_devices();
    cdev_snap_exit();
    bdev_auth_exit();

    pr_info("%s: module removed\n", MOD_NAME);
}

module_init(bdevsnapshot_init);
module_exit(bdevsnapshot_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matteo Basili <matteo.basili@students.uniroma2.eu>");
MODULE_DESCRIPTION("Snapshot service for block devices hosting file systems. In this initial version, only snapshots of the minimal file system (SINGLEFILE-FS) are supported, and restore is available for device files only.");
MODULE_VERSION("1.0.0");

