#include <linux/cdev.h>
#include <linux/version.h>

#include "cdev_snap.h"
#include "snap_ioctl.h"
#include "uapi/bdev_snapshot.h"

static dev_t snap_dev_num;
static struct cdev snap_cdev;
static struct class *snap_class;
static struct device *snap_device;

/* File operations for the snapshot character device */
static const struct file_operations snap_fops = {
	.owner = THIS_MODULE,
	.open = snap_dev_open,
	.release = snap_dev_release,
	.unlocked_ioctl = snap_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = snap_dev_ioctl,
#endif
};

/* Device node permission handler — only root can access (0600) */
static char *snap_devnode(const struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = 0600;  /* rw------- : root only */
    return NULL;
}

/* Initialize and register the snapshot char device */
int cdev_snap_init(void)
{
    int ret = 0;
    
    /* Allocate a major/minor number */
    ret = alloc_chrdev_region(&snap_dev_num, 0, 1, MOD_NAME);
    if (ret) {
	pr_err("%s: alloc_chrdev_region failed (%d)\n", MOD_NAME, ret);
	return ret;
    }

    /* Initialize cdev structure */
    cdev_init(&snap_cdev, &snap_fops);
    snap_cdev.owner = THIS_MODULE;
    
    ret = cdev_add(&snap_cdev, snap_dev_num, 1);
    if (ret) {
	pr_err("%s: cdev_add failed (%d)\n", MOD_NAME, ret);
	goto err_cdev;
    }

    /* Create device class */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,4,0)
    snap_class = class_create(THIS_MODULE, SNAP_DEVICE_FILE);
#else
    snap_class = class_create(SNAP_DEVICE_FILE);
#endif
    if (IS_ERR(snap_class)) {
	ret = PTR_ERR(snap_class);
	pr_err("%s: class_create failed (%d)\n", MOD_NAME, ret);
	goto err_class;
    }
    
    /* Assign custom devnode permissions callback */
    snap_class->devnode = snap_devnode;

    /* Create device node under /dev */
    snap_device = device_create(snap_class, NULL, snap_dev_num, NULL, SNAP_DEVICE_FILE);
    if (IS_ERR(snap_device)) {
	ret = PTR_ERR(snap_device);
	pr_err("%s: device_create failed (%d)\n", MOD_NAME, ret);
	goto err_device;
    }
    
    pr_info("%s: char device registered at %s (major=%d minor=%d)\n",
            MOD_NAME, SNAP_DEVICE_PATH, MAJOR(snap_dev_num), MINOR(snap_dev_num));
    return 0;
        
    /* --- Error paths --- */    
err_device:
    class_destroy(snap_class);
    snap_class = NULL;
err_class:
    cdev_del(&snap_cdev);
err_cdev:
    unregister_chrdev_region(snap_dev_num, 1);	
    return ret;    
}

/* Cleanup and unregister the snapshot char device */
void cdev_snap_exit(void)
{
    if (snap_device) {
        device_destroy(snap_class, snap_dev_num);
        snap_device = NULL;
    }
    if (snap_class) {
        class_destroy(snap_class);
        snap_class = NULL;
    }
        
    cdev_del(&snap_cdev);
    unregister_chrdev_region(snap_dev_num, 1);
    
    pr_debug("%s: char device unregistered\n", MOD_NAME);
}

