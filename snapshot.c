#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/kmod.h> // per call_usermodehelper
#include <linux/timekeeping.h> // per timestamp
#include "snapshot.h"

#define DEVICE_NAME "snapshot"
#define CLASS_NAME  "snapclass"

static int major;
static struct class* snapshot_class = NULL;
static struct device* snapshot_device = NULL;

static char activated_device[256] = {0}; // solo un device per MVP

// Funzione per creare la cartella snapshot in /snapshot/<devname>-<timestamp>
static int create_snapshot_folder(const char *devname) {
    char path[256];
    char ts[32];
    unsigned long sec;
    struct timespec64 tspec;
    int rc;

    // ottieni tempo corrente in secondi
    ktime_get_real_ts64(&tspec);
    sec = tspec.tv_sec;

    // costruisci stringa timestamp YYYYMMDD-HHMMSS
    snprintf(ts, sizeof(ts), "%08lu-%06lu", sec / 86400, sec % 86400);

    // costruisci path completo
    snprintf(path, sizeof(path), "/snapshot/%s-%s", devname, ts);

    {
        char *argv[] = { "/usr/bin/mkdir", "-p", path, NULL };
        char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/bin", NULL };
        rc = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
        pr_info("snapshot: mkdir -p %s returned %d\n", path, rc);
        return rc;
    }
}

static long snapshot_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    char user_input[256];

    if (copy_from_user(user_input, (char __user *)arg, sizeof(user_input)))
        return -EFAULT;

    user_input[sizeof(user_input)-1] = '\0'; // safe

    switch(cmd) {
        case SNAPSHOT_ACTIVATE:
            if (strcmp(user_input, SNAPSHOT_PASSWD) == 0) {
                strncpy(activated_device, "demo_device", sizeof(activated_device));
                pr_info("Snapshot attivato per device: %s\n", activated_device);

                // CREA CARTELLA SNAPSHOT
                create_snapshot_folder(activated_device);

            } else {
                pr_warn("Password errata in activate\n");
                return -EACCES;
            }
            break;

        case SNAPSHOT_DEACTIVATE:
            if (strcmp(user_input, SNAPSHOT_PASSWD) == 0) {
                pr_info("Snapshot disattivato per device: %s\n", activated_device);
                activated_device[0] = '\0';
            } else {
                pr_warn("Password errata in deactivate\n");
                return -EACCES;
            }
            break;

        default:
            return -EINVAL;
    }
    return 0;
}

static struct file_operations fops = {
    .unlocked_ioctl = snapshot_ioctl,
    .owner = THIS_MODULE,
};

static int __init snapshot_init(void) {
    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) {
        pr_err("Impossibile registrare device\n");
        return major;
    }
    
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    snapshot_class = class_create(CLASS_NAME);
#else
    snapshot_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(snapshot_class)) {
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(snapshot_class);
    }

    snapshot_device = device_create(snapshot_class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(snapshot_device)) {
        class_destroy(snapshot_class);
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(snapshot_device);
    }

    pr_info("Modulo snapshot caricato, major=%d\n", major);
    return 0;
}

static void __exit snapshot_exit(void) {
    device_destroy(snapshot_class, MKDEV(major, 0));
    class_destroy(snapshot_class);
    unregister_chrdev(major, DEVICE_NAME);
    pr_info("Modulo snapshot rimosso\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tu");
MODULE_DESCRIPTION("MVP snapshot LKM con ioctl e directory /snapshot");
module_init(snapshot_init);
module_exit(snapshot_exit);

