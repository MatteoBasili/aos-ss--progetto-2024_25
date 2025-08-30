#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/capability.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include "bdev_snapshot.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
#error "This module requires at least the 6.3.0 kernel version."
#endif

/* Stato del char device */
static dev_t dev_num;                /* major:minor allocato dinamicamente */
static struct cdev bdev_cdev;        /* cdev per /dev/bdev_snapshot */
static struct class *bdev_class;     /* classe udev */
static struct device *bdev_device;   /* device udev */

/* Password storage: SHA-256 (32 byte) */
#define PW_HASH_LEN 32
static u8 pw_hash[PW_HASH_LEN];
static bool pw_set = false;
static DEFINE_MUTEX(pw_mutex);

/* crypto tfm for sha256 (allocated at init, freed at exit) */
static struct crypto_shash *sha_tfm = NULL;

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

/* --- Helper: compute sha256 --- */
static int compute_sha256(const char *buf, size_t len, u8 *out)
{
    struct shash_desc *sdesc;
    int desc_size;
    int ret = 0;

    if (!sha_tfm)
        return -EINVAL;

    desc_size = sizeof(*sdesc) + crypto_shash_descsize(sha_tfm);
    sdesc = kmalloc(desc_size, GFP_KERNEL);
    if (!sdesc)
        return -ENOMEM;

    sdesc->tfm = sha_tfm;

    ret = crypto_shash_init(sdesc);
    if (ret) goto out;

    ret = crypto_shash_update(sdesc, buf, len);
    if (ret) goto out;

    ret = crypto_shash_final(sdesc, out);

out:
    kfree(sdesc);
    return ret;
}

/* --- Password helpers --- */
static int set_password_from_user(const char *pw, size_t pwlen)
{
    int ret;
    u8 tmp_hash[PW_HASH_LEN];

    if (pwlen == 0 || pwlen > 63)
        return -EINVAL;

    ret = compute_sha256(pw, pwlen, tmp_hash);
    if (ret)
        return ret;

    mutex_lock(&pw_mutex);
    memcpy(pw_hash, tmp_hash, PW_HASH_LEN);
    pw_set = true;
    mutex_unlock(&pw_mutex);

    /* zero tmp_hash just in case */
    memzero_explicit(tmp_hash, PW_HASH_LEN);
    return 0;
}

static bool verify_password_from_user(const char *pw, size_t pwlen)
{
    int ret;
    u8 tmp_hash[PW_HASH_LEN];
    bool match = false;

    if (!pw_set)
        return false;

    if (pwlen == 0 || pwlen > 63)
        return false;

    ret = compute_sha256(pw, pwlen, tmp_hash);
    if (ret)
        return false;

    mutex_lock(&pw_mutex);
    match = (memcmp(pw_hash, tmp_hash, PW_HASH_LEN) == 0);
    mutex_unlock(&pw_mutex);

    memzero_explicit(tmp_hash, PW_HASH_LEN);
    return match;
}

/* --- Implementazione --- */

static int bdev_open(struct inode *inode, struct file *file)
{
    pr_info("%s: device opened\n", MOD_NAME);
    return 0;
}

static int bdev_release(struct inode *inode, struct file *file)
{
    pr_info("%s: device closed\n", MOD_NAME);
    return 0;
}

static long bdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct snap_args args;
    int ret;
    size_t pwlen;

    /* richiediamo CAP_SYS_ADMIN (root) */
    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
        return -EFAULT;

    /* sicurezza: assicurarsi che strings siano terminate */
    args.dev_name[sizeof(args.dev_name)-1] = '\0';
    args.password[sizeof(args.password)-1] = '\0';
    pwlen = strnlen(args.password, sizeof(args.password));

    switch (cmd) {
    case SNAP_ACTIVATE:
        /* Se password non impostata, la prima activation la imposta */
        if (!pw_set) {
            ret = set_password_from_user(args.password, pwlen);
            if (ret) {
                pr_err("%s: failed to set password (err=%d)\n", MOD_NAME, ret);
                goto out_zero_pw;
            }
            pr_info("%s: password set (first-time). Snapshot ACTIVATED on %s\n",
                    MOD_NAME, args.dev_name);
            /* TODO: registrare il device e preparare struttura snapshot */
            ret = 0;
            goto out_zero_pw;
        }

        /* password già impostata: verifica */
        if (!verify_password_from_user(args.password, pwlen)) {
            pr_warn("%s: SNAP_ACTIVATE authentication failed for device %s\n",
                    MOD_NAME, args.dev_name);
            ret = -EACCES;
            goto out_zero_pw;
        }

        pr_info("%s: SNAP_ACTIVATE authenticated for device %s\n",
                MOD_NAME, args.dev_name);
        /* TODO: registrare il device e preparare struttura snapshot */
        ret = 0;
        goto out_zero_pw;

    case SNAP_DEACTIVATE:
        if (!pw_set) {
            pr_warn("%s: SNAP_DEACTIVATE requested but no password set\n", MOD_NAME);
            ret = -EINVAL;
            goto out_zero_pw;
        }

        if (!verify_password_from_user(args.password, pwlen)) {
            pr_warn("%s: SNAP_DEACTIVATE authentication failed for device %s\n",
                    MOD_NAME, args.dev_name);
            ret = -EACCES;
            goto out_zero_pw;
        }

        pr_info("%s: SNAP_DEACTIVATE authenticated for device %s\n",
                MOD_NAME, args.dev_name);
        /* TODO: rimuovere dalla lista dispositivi attivi */
        ret = 0;
        goto out_zero_pw;

    default:
        ret = -ENOTTY;
        goto out_zero_pw;
    }

out_zero_pw:
    /* Clear sensitive data in kernel stack copy before returning */
    memzero_explicit(args.password, sizeof(args.password));
    return ret;
}

/* --- INIT / EXIT --- */

static int __init bdevsnapshot_init(void)
{
    int ret;

    pr_info("%s: loading block-device snapshot module\n", MOD_NAME);

    /* initialize sha tfm */
    sha_tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(sha_tfm)) {
        pr_err("%s: crypto_alloc_shash failed\n", MOD_NAME);
        sha_tfm = NULL;
        return PTR_ERR(sha_tfm);
    }

    /* 1) Alloca major/minor dinamico */
    ret = alloc_chrdev_region(&dev_num, 0, 1, MOD_NAME);
    if (ret) {
        pr_err("%s: alloc_chrdev_region failed: %d\n", MOD_NAME, ret);
        crypto_free_shash(sha_tfm);
        sha_tfm = NULL;
        return ret;
    }

    /* 2) Inizializza e registra la cdev */
    cdev_init(&bdev_cdev, &bdev_fops);
    bdev_cdev.owner = THIS_MODULE;

    ret = cdev_add(&bdev_cdev, dev_num, 1);
    if (ret) {
        pr_err("%s: cdev_add failed: %d\n", MOD_NAME, ret);
        unregister_chrdev_region(dev_num, 1);
        crypto_free_shash(sha_tfm);
        sha_tfm = NULL;
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
        pr_err("%s: class_create failed: %d\n", MOD_NAME, ret);
        cdev_del(&bdev_cdev);
        unregister_chrdev_region(dev_num, 1);
        crypto_free_shash(sha_tfm);
        sha_tfm = NULL;
        return ret;
    }

    bdev_device = device_create(bdev_class, NULL, dev_num, NULL, "bdev_snapshot");
    if (IS_ERR(bdev_device)) {
        ret = PTR_ERR(bdev_device);
        pr_err("%s: device_create failed: %d\n", MOD_NAME, ret);
        class_destroy(bdev_class);
        cdev_del(&bdev_cdev);
        unregister_chrdev_region(dev_num, 1);
        crypto_free_shash(sha_tfm);
        sha_tfm = NULL;
        return ret;
    }

    pr_info("%s: device node /dev/bdev_snapshot ready (major=%d minor=%d)\n",
            MOD_NAME, MAJOR(dev_num), MINOR(dev_num));
    return 0;
}

static void __exit bdevsnapshot_exit(void)
{
    /* zero stored hash before unload */
    mutex_lock(&pw_mutex);
    if (pw_set)
        memzero_explicit(pw_hash, PW_HASH_LEN);
    pw_set = false;
    mutex_unlock(&pw_mutex);

    device_destroy(bdev_class, dev_num);
    class_destroy(bdev_class);
    cdev_del(&bdev_cdev);
    unregister_chrdev_region(dev_num, 1);

    if (sha_tfm) {
        crypto_free_shash(sha_tfm);
        sha_tfm = NULL;
    }

    pr_info("%s: removed block-device snapshot module\n", MOD_NAME);
}

module_init(bdevsnapshot_init);
module_exit(bdevsnapshot_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matteo Basili <matteo.basili@students.uniroma2.eu>");
MODULE_DESCRIPTION("Snapshot service for block devices hosting file systems - password auth (F3)");

