#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>      /* For LINUX_VERSION_CODE */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/capability.h>

#include "bdev_snapshot.h"


#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
#error "This module requires at least the 6.3.0 kernel version."
#endif


/* Stato del char device */
static dev_t dev_num;                /* major:minor allocato dinamicamente */
static struct cdev bdev_cdev;        /* cdev per /dev/bdev_snapshot */
static struct class *bdev_class;     /* classe udev */
static struct device *bdev_device;   /* device udev */

/* --- Prototipi --- */
static long bdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int  bdev_open(struct inode *inode, struct file *file);
static int  bdev_release(struct inode *inode, struct file *file);

/* --- File operations --- */
static const struct file_operations bdev_fops = {
    .owner          = THIS_MODULE,
    .open           = bdev_open,
    .release        = bdev_release,
    .unlocked_ioctl = bdev_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl   = bdev_ioctl,
#endif
};

/* --- Implementazione --- */

static int bdev_open(struct inode *inode, struct file *file)
{
    printk("%s: device opened\n", MOD_NAME);
    return 0;
}

static int bdev_release(struct inode *inode, struct file *file)
{
    printk("%s: device closed\n", MOD_NAME);
    return 0;
}

static long bdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct snap_args args;

    /* F3: aggiungeremo password hashing/verify; per ora richiediamo root */
    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    switch (cmd) {
    case SNAP_ACTIVATE:
        if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
            return -EFAULT;

        printk("%s: SNAP_ACTIVATE dev=\"%s\" (pw=\"%s\")\n",
                MOD_NAME, args.dev_name, args.password);

        /* TODO (F4-F7): registrare il device e preparare struttura snapshot */
        return 0;

    case SNAP_DEACTIVATE:
        if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
            return -EFAULT;

        printk("%s: SNAP_DEACTIVATE dev=\"%s\" (pw=\"%s\")\n",
                MOD_NAME, args.dev_name, args.password);

        /* TODO (F4): rimuovere dalla lista dispositivi attivi */
        return 0;

    default:
        return -ENOTTY; /* comando non riconosciuto */
    }
}

/* --- INIT / EXIT --- */

static int __init bdevsnapshot_init(void) {

    int ret;

    printk("%s: loaded block-device snapshot module\n", MOD_NAME);
    
    /* 1) Alloca major/minor dinamico */
    ret = alloc_chrdev_region(&dev_num, 0, 1, MOD_NAME);
    if (ret) {
        printk("%s: alloc_chrdev_region failed: %d\n", MOD_NAME, ret);
        return ret;
    }

    /* 2) Inizializza e registra la cdev */
    cdev_init(&bdev_cdev, &bdev_fops);
    bdev_cdev.owner = THIS_MODULE;

    ret = cdev_add(&bdev_cdev, dev_num, 1);
    if (ret) {
        printk("%s: cdev_add failed: %d\n", MOD_NAME, ret);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    /* 3) Crea classe e device node /dev/bdev_snapshot */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,4,0)
    bdev_class = class_create(THIS_MODULE, "bdev_snapshot");
#else
    bdev_class = class_create("bdev_snapshot");
#endif    
    if (IS_ERR(bdev_class)) {
        ret = PTR_ERR(bdev_class);
        printk("%s: class_create failed: %d\n", MOD_NAME, ret);
        cdev_del(&bdev_cdev);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    bdev_device = device_create(bdev_class, NULL, dev_num, NULL, "bdev_snapshot");
    if (IS_ERR(bdev_device)) {
        ret = PTR_ERR(bdev_device);
        printk("%s: device_create failed: %d\n", MOD_NAME, ret);
        class_destroy(bdev_class);
        cdev_del(&bdev_cdev);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    printk("%s: device node /dev/bdev_snapshot ready (major=%d minor=%d)\n",
            MOD_NAME, MAJOR(dev_num), MINOR(dev_num));
    
    return 0;
}

static void __exit bdevsnapshot_exit(void) {

    device_destroy(bdev_class, dev_num);
    class_destroy(bdev_class);
    cdev_del(&bdev_cdev);
    unregister_chrdev_region(dev_num, 1);

    printk("%s: removed block-device snapshot module\n", MOD_NAME);
}

module_init(bdevsnapshot_init);
module_exit(bdevsnapshot_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matteo Basili <matteo.basili@students.uniroma2.eu>");
MODULE_DESCRIPTION("Snapshot service for block devices hosting file systems");

