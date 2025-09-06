#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/version.h>

#include "bdev_snapshot.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
#error "This module requires at least the 6.3.0 kernel version."
#endif

static dev_t dev_num;
static struct cdev bdev_cdev;
static struct class *bdev_class;
static struct device *bdev_device;

static const struct file_operations bdev_fops = {
	.owner = THIS_MODULE,
	.open = bdev_open,
	.release = bdevsnap_release,
	.unlocked_ioctl = bdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = bdev_ioctl,
#endif
};

static int __init bdevsnapshot_init(void)
{
	int ret;

	pr_info("%s: loading module\n", MOD_NAME);

	/* --- Auth subsystem --- */
	ret = bdev_auth_init();
	if (ret) {
		pr_err("%s: bdev_auth_init failed: %d\n", MOD_NAME, ret);
		return ret;
	}

	/* --- Store snapshot --- */
	/*ret = bdev_store_global_init();
	if (ret) {
		pr_err("%s: bdev_store_global_init failed: %d\n", MOD_NAME, ret);
		goto err_pw;
	}*/

	/* --- Character device --- */
	ret = alloc_chrdev_region(&dev_num, 0, 1, MOD_NAME);
	if (ret) {
		pr_err("%s: alloc_chrdev_region failed: %d\n", MOD_NAME, ret);
		goto err_store;
	}

	cdev_init(&bdev_cdev, &bdev_fops);
	bdev_cdev.owner = THIS_MODULE;
	ret = cdev_add(&bdev_cdev, dev_num, 1);
	if (ret) {
		pr_err("%s: cdev_add failed: %d\n", MOD_NAME, ret);
		goto err_chrdev;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,4,0)
	bdev_class = class_create(THIS_MODULE, "bdev_snapctl");
#else
	bdev_class = class_create("bdev_snapctl");
#endif
	if (IS_ERR(bdev_class)) {
		ret = PTR_ERR(bdev_class);
		pr_err("%s: class_create failed: %d\n", MOD_NAME, ret);
		goto err_cdev;
	}

	bdev_device = device_create(bdev_class, NULL, dev_num, NULL, "bdev_snapctl");
	if (IS_ERR(bdev_device)) {
		ret = PTR_ERR(bdev_device);
		pr_err("%s: device_create failed: %d\n", MOD_NAME, ret);
		goto err_class;
	}

	/* --- Kprobe per intercettare submit_bio --- */
	ret = bdev_kprobe_init();
	if (ret) {
		pr_err("%s: kprobe init failed: %d\n", MOD_NAME, ret);
		goto err_dev;
	}

	pr_info("%s: /dev/bdev_snapctl ready (major=%d minor=%d)\n",
		MOD_NAME, MAJOR(dev_num), MINOR(dev_num));
	return 0;

	/* --- Error paths --- */
err_dev:
	device_destroy(bdev_class, dev_num);
err_class:
	class_destroy(bdev_class);
err_cdev:
	cdev_del(&bdev_cdev);
err_chrdev:
	unregister_chrdev_region(dev_num, 1);
err_store:
	//bdev_store_global_exit();
//err_pw:
	bdev_auth_exit();
	return ret;
}

static void __exit bdevsnapshot_exit(void)
{
	/* Rimuovi kprobe prima di distruggere altre risorse */
	bdev_kprobe_exit();

	/* Cleanup store */
	//bdev_store_global_exit();

	/* Pulizia e snapshot */
	clear_snap_devices();

	device_destroy(bdev_class, dev_num);
	class_destroy(bdev_class);
	cdev_del(&bdev_cdev);
	unregister_chrdev_region(dev_num, 1);

	bdev_auth_exit();

	pr_info("%s: module removed\n", MOD_NAME);
}

module_init(bdevsnapshot_init);
module_exit(bdevsnapshot_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matteo Basili <matteo.basili@students.uniroma2.eu>");
MODULE_DESCRIPTION("Snapshot service for block devices hosting file systems");

