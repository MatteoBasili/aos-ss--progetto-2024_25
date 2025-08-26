#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/kmod.h>       // call_usermodehelper
#include <linux/timekeeping.h>
#include <linux/kprobes.h>    // kprobes
#include <linux/bio.h>        // bio structure
#include <linux/workqueue.h>  // workqueue
#include "snapshot.h"

#define DEVICE_NAME "snapshot"
#define CLASS_NAME  "snapclass"

static int major;
static struct class* snapshot_class = NULL;
static struct device* snapshot_device = NULL;

static char activated_device[256] = {0}; 
static char snapshot_dir[256] = {0};     

// Workqueue
static struct workqueue_struct *snap_wq;

// Struttura per un blocco intercettato
struct snap_work {
    struct work_struct work;
    sector_t sector;
    unsigned int len;
    char *data;
    char devname[64];
};

// Worker che scrive il blocco intercettato su file usando kernel_write
static void snap_worker(struct work_struct *work) {
    struct snap_work *sw = container_of(work, struct snap_work, work);
    char filepath[512];
    loff_t offset = 0;
    struct file *f;
    int ret;

    snprintf(filepath, sizeof(filepath), "%s/%s_sector_%llu.bin",
             snapshot_dir, sw->devname, (unsigned long long)sw->sector);

    f = filp_open(filepath, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (IS_ERR(f)) {
        pr_err("snapshot: impossibile aprire file %s\n", filepath);
        goto out;
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
    mm_segment_t oldfs = get_fs();
    set_fs(KERNEL_DS);
#endif

    ret = kernel_write(f, sw->data, sw->len, &offset);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
    set_fs(oldfs);
#endif

    if (ret != sw->len)
        pr_warn("snapshot: scrittura incompleta blocco %llu (ret=%d)\n",
                (unsigned long long)sw->sector, ret);

    filp_close(f, NULL);

    pr_info("snapshot: scritto blocco sector=%llu len=%u su %s\n",
            (unsigned long long)sw->sector, sw->len, filepath);

out:
    kfree(sw->data);
    kfree(sw);
}

// Handler kprobe su submit_bio
static int handler_pre(struct kprobe *p, struct pt_regs *regs) {
    struct bio *bio;
    struct bio_vec bvec;
    struct bvec_iter iter;
    void *kaddr;
    struct snap_work *sw;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,18,0)
    bio = (struct bio *)regs->di; // su x86_64, primo argomento in rdi
#else
    bio = (struct bio *)regs->si; // fallback
#endif

    if (!bio || bio_op(bio) != REQ_OP_WRITE)
        return 0;

    // Se snapshot non attivo, esci subito
    if (activated_device[0] == '\0')
        return 0;

    // Itera su tutti i segmenti BIO
    bio_for_each_segment(bvec, bio, iter) {
        kaddr = kmap_atomic(bvec.bv_page);
        sw = kmalloc(sizeof(*sw), GFP_ATOMIC);
        if (!sw) {
            kunmap_atomic(kaddr);
            continue; // passa al segmento successivo
        }

        INIT_WORK(&sw->work, snap_worker);
        sw->sector = iter.bi_sector;
        sw->len = bvec.bv_len;
        sw->data = kmalloc(sw->len, GFP_ATOMIC);
        strncpy(sw->devname, activated_device, sizeof(sw->devname));

        if (sw->data)
            memcpy(sw->data, kaddr + bvec.bv_offset, sw->len);

        kunmap_atomic(kaddr);

        // Metti in coda nella workqueue
        queue_work(snap_wq, &sw->work);
    }

    return 0;
}

static struct kprobe kp = {
    .symbol_name = "submit_bio",
    .pre_handler = handler_pre,
};

// Funzione per creare la cartella snapshot
static int create_snapshot_folder(const char *devname) {
    char ts[32];
    unsigned long sec;
    struct timespec64 tspec;
    int rc;

    ktime_get_real_ts64(&tspec);
    sec = tspec.tv_sec;

    snprintf(ts, sizeof(ts), "%lu", sec);
    snprintf(snapshot_dir, sizeof(snapshot_dir), "/snapshot/%s-%s", devname, ts);

    {
        char *argv[] = { "/usr/bin/mkdir", "-p", snapshot_dir, NULL };
        char *envp[] = { "HOME=/", "PATH=/sbin:/bin:/usr/bin", NULL };
        rc = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
        pr_info("snapshot: mkdir -p %s returned %d\n", snapshot_dir, rc);
        return rc;
    }
}

static long snapshot_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    char user_input[256];

    if (copy_from_user(user_input, (char __user *)arg, sizeof(user_input)))
        return -EFAULT;

    user_input[sizeof(user_input)-1] = '\0';

    switch(cmd) {
        case SNAPSHOT_ACTIVATE:
            if (strcmp(user_input, SNAPSHOT_PASSWD) == 0) {
                strncpy(activated_device, "demo_device", sizeof(activated_device));
                pr_info("Snapshot attivato per device: %s\n", activated_device);
                create_snapshot_folder(activated_device);
            } else {
                return -EACCES;
            }
            break;

        case SNAPSHOT_DEACTIVATE:
            if (strcmp(user_input, SNAPSHOT_PASSWD) == 0) {
                pr_info("Snapshot disattivato per device: %s\n", activated_device);
                activated_device[0] = '\0';
            } else {
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
    int ret;

    snap_wq = alloc_workqueue("snap_wq", WQ_UNBOUND, 0);
    if (!snap_wq)
        return -ENOMEM;

    ret = register_kprobe(&kp);
    if (ret < 0) {
        pr_err("register_kprobe failed, returned %d\n", ret);
        return ret;
    }

    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) {
        unregister_kprobe(&kp);
        return major;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    snapshot_class = class_create(CLASS_NAME);
#else
    snapshot_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(snapshot_class)) {
        unregister_chrdev(major, DEVICE_NAME);
        unregister_kprobe(&kp);
        return PTR_ERR(snapshot_class);
    }

    snapshot_device = device_create(snapshot_class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(snapshot_device)) {
        class_destroy(snapshot_class);
        unregister_chrdev(major, DEVICE_NAME);
        unregister_kprobe(&kp);
        return PTR_ERR(snapshot_device);
    }

    pr_info("Modulo snapshot con kprobes caricato\n");
    return 0;
}

static void __exit snapshot_exit(void) {
    flush_workqueue(snap_wq);
    destroy_workqueue(snap_wq);
    unregister_kprobe(&kp);
    device_destroy(snapshot_class, MKDEV(major, 0));
    class_destroy(snapshot_class);
    unregister_chrdev(major, DEVICE_NAME);
    pr_info("Modulo snapshot rimosso\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tu");
MODULE_DESCRIPTION("Snapshot MVP con ioctl + kprobes + workqueue");
module_init(snapshot_init);
module_exit(snapshot_exit);

