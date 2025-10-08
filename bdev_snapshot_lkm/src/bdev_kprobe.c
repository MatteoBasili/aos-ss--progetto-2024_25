#include <linux/blkdev.h>
#include <linux/kprobes.h>
#include <linux/major.h>

#include "bdev_fs.h"
#include "bdev_kprobe.h"
#include "bdev_list.h"
#include "snap_store.h"
#include "snap_utils.h"

/* ================= Kprobe Structs ================= */
static struct kretprobe rp_mount;
static struct kretprobe rp_unmount;
static struct kretprobe rp_write_fs;

/* ================= Helper functions ================= */

/* Retrieve device name for snapshot handling (loop or regular block device) */
static int get_snap_dev_name(struct block_device *bdev, char *buf, size_t sz)
{
    if (!bdev || !buf || sz == 0)
        return -EINVAL;

    if (MAJOR(bdev->bd_dev) == LOOP_MAJOR)
        return get_loop_backing_path(bdev, buf, sz);

    get_bdev_name(bdev, buf, sz);
    return 0;
}

/* ================= Workqueue Handlers ================= */

/* Handler for mount work */
static void mount_work_handler(struct work_struct *work)
{
    struct mount_work *mw = container_of(work, struct mount_work, work);
    struct snap_device *dev;

    dev = snap_find_device_get(mw->dev_name);
    if (dev) {
        int ret = snapdev_do_mount_work(dev);
        if (ret == 0) {
            pr_info("%s: snapshot opened for %s\n", MOD_NAME, mw->dev_name);
        } else {
            pr_warn("%s: failed to complete mount for %s (ret=%d)\n",
                    MOD_NAME, mw->dev_name, ret);
        }        
        snap_device_put(dev);
    }

    kfree(mw);
}

/* Handler for unmount work */
static void unmount_work_handler(struct work_struct *work)
{
    struct unmount_work *uw = container_of(work, struct unmount_work, work);
    struct snap_device *dev;

    if (!*uw->dev_name || uw->unmount_ret < 0)
        goto out;

    dev = snap_find_device_get(uw->dev_name);
    if (dev) {
        int ret = snapdev_do_unmount_work(dev);
        if (ret == 0) {
            pr_info("%s: unmount successful, snapshot closed for %s\n",
                    MOD_NAME, uw->dev_name);
        } else {
            pr_warn("%s: failed to complete unmount for %s (ret=%d)\n",
                    MOD_NAME, uw->dev_name, ret);
        }
        snap_device_put(dev);
    }

out:
    kfree(uw);
}

/* ================= Workqueue Scheduling ================= */

/* Generic function to schedule mount work */
static void schedule_mount_work(const char *dev_name)
{
    struct mount_work *mw = kmalloc(sizeof(*mw), GFP_ATOMIC);
    struct snap_device *dev;

    if (!mw)
        return;

    INIT_WORK(&mw->work, mount_work_handler);
    strscpy(mw->dev_name, dev_name, DEV_NAME_LEN_MAX);

    dev = snap_find_device_get(dev_name);
    if (!dev) {
        kfree(mw);
        return;
    }

    queue_work(dev->wq, &mw->work);
    snap_device_put(dev);
}

/* Generic function to schedule unmount work */
static void schedule_unmount_work(const char *dev_name, long ret)
{
    struct unmount_work *uw = kmalloc(sizeof(*uw), GFP_ATOMIC);
    struct snap_device *dev;

    if (!uw)
        return;

    INIT_WORK(&uw->work, unmount_work_handler);
    strscpy(uw->dev_name, dev_name, DEV_NAME_LEN_MAX);
    uw->unmount_ret = ret;

    dev = snap_find_device_get(dev_name);
    if (!dev) {
        kfree(uw);
        return;
    }

    queue_work(dev->wq, &uw->work);
    snap_device_put(dev);
}

/* ================= Mount/Unmount Kretprobe Handlers ================= */

static void handle_mount_device(struct block_device *bdev)
{
    char snap_name[DEV_NAME_LEN_MAX] = {0};
    struct snap_device *dev;
    int ret;

    if (!bdev)
        return;

    if (get_snap_dev_name(bdev, snap_name, sizeof(snap_name)) != 0)
        return;

    dev = snap_find_device_get(snap_name);
    if (!dev)
        return;

    ret = snapdev_mark_mounted(dev);
    if (ret == 0) {            
        schedule_mount_work(snap_name);
    } else if (ret == -EBUSY) {
        pr_info("%s: device %s is already mounted, snapshot already active\n",
                MOD_NAME, snap_name);
    } else if (ret == -EPERM) {
        pr_warn("%s: snapshot for device %s is disabled, cannot mark mounted\n",
                MOD_NAME, snap_name);
    } else {
        pr_warn("%s: failed to mark device %s as mounted, ret=%d\n",
                MOD_NAME, snap_name, ret);
    }

    snap_device_put(dev);
}

static int mount_bdev_ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct dentry *root = (struct dentry *)regs_return_value(regs);

    if (!root || IS_ERR(root))
        return 0;

    if (root->d_sb && root->d_sb->s_bdev) {
        struct super_block *sb = root->d_sb;

        if (sb->s_flags & SB_RDONLY) {
            pr_debug("%s: detected read-only mount on %s, skipping snapshot\n",
                     MOD_NAME, sb->s_id);
            return 0;
        }

        handle_mount_device(sb->s_bdev);
    }

    return 0;
}

static int kill_sb_entry_handler(struct kretprobe_instance *ri,
                                 struct pt_regs *regs)
{
    struct super_block *sb = (struct super_block *)PT_REGS_PARM1(regs);
    struct umount_kretprobe_metadata *meta =
        (struct umount_kretprobe_metadata *)ri->data;

    if (meta && sb && sb->s_bdev) {
        if (get_snap_dev_name(sb->s_bdev, meta->dev_name, sizeof(meta->dev_name)) != 0)
            meta->dev_name[0] = '\0';
    }

    return 0;
}

static int kill_sb_ret_handler(struct kretprobe_instance *ri,
                               struct pt_regs *regs)
{
    struct umount_kretprobe_metadata *meta =
        (struct umount_kretprobe_metadata *)ri->data;

    if (meta) {
        struct snap_device *dev;
        int ret;
    
        dev = snap_find_device_get(meta->dev_name);
        if (!dev)
            return 0;
    
        ret = snapdev_mark_unmounted(dev);
        if (ret == 0) {
            schedule_unmount_work(meta->dev_name, 0);
        } else if (ret == -EINVAL) {
            pr_debug("%s: device %s was not mounted, nothing to unmount\n", MOD_NAME, meta->dev_name);
        } else {
            pr_warn("%s: failed to mark device %s as unmounted, ret=%d\n", MOD_NAME, meta->dev_name, ret);
        }   

        snap_device_put(dev);
       
    }

    return 0;
}

/* ================= VFS Write Kretprobe Handlers ================= */

static void populate_singlefilefs_write_metadata(struct singlefilefs_write_kretprobe_metadata *meta,
                                    struct snap_device *sdev,
                                    struct inode *inode,
                                    loff_t *offptr)
{
    if (!meta || !sdev || !inode || !offptr)
        return;

    meta->original_offset = *offptr;
    meta->inode = inode;
    meta->original_inode_size = i_size_read(inode);
    meta->pending_blocks = snap_prepare_singlefilefs_block_save(sdev, inode, offptr);
}


static int vfs_write_entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct singlefilefs_write_kretprobe_metadata *meta;
    struct file *filp = (struct file *)PT_REGS_PARM1(regs);
    size_t len = (size_t)PT_REGS_PARM3(regs);
    loff_t *offptr = (loff_t *)PT_REGS_PARM4(regs);

    struct inode *inode;
    struct snap_device *sdev = NULL;
    char dev_name[DEV_NAME_LEN_MAX];

    if (!filp || !offptr || len == 0)
        return 0;

    inode = filp->f_inode;
    if (!inode || !inode->i_sb || !inode->i_sb->s_bdev)
        return 0;
    
    if (bdev_read_only(inode->i_sb->s_bdev))
        return 0;

    if (get_snap_dev_name(inode->i_sb->s_bdev, dev_name, sizeof(dev_name)) != 0)
        return 0;

    sdev = snap_find_device_get(dev_name);
    if (!sdev)
        return 0;

    if (!snapdev_is_mounted(sdev)) {
        snap_device_put(sdev);
        return 0;
    }

    meta = (struct singlefilefs_write_kretprobe_metadata *)ri->data;
    populate_singlefilefs_write_metadata(meta, sdev, inode, offptr);

    snap_device_put(sdev);
    return 0;
}

/* This is only valid for the singlefilefs */
static int vfs_write_ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct singlefilefs_write_kretprobe_metadata *meta = (struct singlefilefs_write_kretprobe_metadata *)ri->data;
    struct snap_pending_block *blk, *next;
    ssize_t ret = (ssize_t)regs_return_value(regs);
    loff_t final_offset;

    if (!meta->pending_blocks)
        return 0;

    blk = meta->pending_blocks;

    /* If writing fails → free pending_block memory */
    if (ret <= 0) {
        while (blk) {
            next = blk->next;
            kfree(blk->data);
            kfree(blk);
            blk = next;
        }
        meta->pending_blocks = NULL;
        return 0;
    }

    final_offset = meta->original_offset + ret;

    /* Writing OK → for each pending_block create a work_struct */
    while (blk) {
        next = blk->next;

        /* Inode lock: schedule only if writing has extended the file */
        if (blk->block_num == SINGLEFILEFS_INODE_BLOCK_NUMBER && final_offset <= meta->original_inode_size) {
            kfree(blk->data);
            kfree(blk);
            blk = next;
            continue;
        }

        /* Check if the block has already been saved */
        if (!snap_try_mark_block_saved(blk->dev, blk->block_num)) {
            struct snap_block_work *bw = kmalloc(sizeof(*bw), GFP_ATOMIC);
            if (bw) {
                bw->dev       = blk->dev;
                bw->block_num = blk->block_num;
                bw->len       = blk->len;
                bw->data      = blk->data;

                snap_device_get(bw->dev);

                INIT_WORK(&bw->work, snap_block_work_handler);
                queue_work(bw->dev->wq, &bw->work);
            } else {
                kfree(blk->data);
            }
        } else {
            kfree(blk->data);
        }

        kfree(blk);
        blk = next;
    }

    meta->pending_blocks = NULL;
    return 0;
}

/* ================= Initialization / Exit ================= */

static int mount_kretprobe_init(void)
{
    int ret;

    rp_mount.kp.symbol_name = "mount_bdev";
    rp_mount.handler = mount_bdev_ret_handler;
    rp_mount.maxactive = 20;
    rp_mount.data_size = 0;

    ret = register_kretprobe(&rp_mount);
    if (ret)
        pr_err("%s: failed to register mount_bdev kretprobe: %d\n", MOD_NAME, ret);
    else
        pr_debug("%s: mount_bdev kretprobe registered\n", MOD_NAME);    
    
    return ret;
}

static void mount_kretprobe_exit(void)
{ 
    unregister_kretprobe(&rp_mount);
}

static int unmount_kretprobe_init(void)
{
    int ret;

    rp_unmount.kp.symbol_name = "kill_block_super";
    rp_unmount.entry_handler = kill_sb_entry_handler;
    rp_unmount.handler = kill_sb_ret_handler;
    rp_unmount.maxactive = 20;
    rp_unmount.data_size = sizeof(struct umount_kretprobe_metadata);

    ret = register_kretprobe(&rp_unmount);
    if (ret)
        pr_err("%s: failed to register kill_block_super kretprobe: %d\n", MOD_NAME, ret);
    else 
        pr_debug("%s: kill_block_super kretprobe registered\n", MOD_NAME);

    return ret;
}

static void unmount_kretprobe_exit(void)
{
    unregister_kretprobe(&rp_unmount);
}

static int vfs_write_kretprobe_init(void)
{
    int ret;

    rp_write_fs.kp.symbol_name = "vfs_write";
    rp_write_fs.entry_handler = vfs_write_entry_handler;
    rp_write_fs.handler = vfs_write_ret_handler;
    rp_write_fs.maxactive = 40;
    rp_write_fs.data_size = sizeof(struct singlefilefs_write_kretprobe_metadata);

    ret = register_kretprobe(&rp_write_fs);
    if (ret)
        pr_err("%s: failed to register vfs_write kretprobe: %d\n", MOD_NAME, ret);
    else
        pr_debug("%s: vfs_write kretprobe registered\n", MOD_NAME);

    return ret;
}

static void vfs_write_kretprobe_exit(void)
{
    unregister_kretprobe(&rp_write_fs);
}

int bdev_kprobe_module_init(void)
{
    int ret;

    ret = mount_kretprobe_init();
    if (ret)
        goto err_mount;

    ret = unmount_kretprobe_init();
    if (ret)
        goto err_unmount;

    ret = vfs_write_kretprobe_init();
    if (ret)
        goto err_vfs;
        
    return 0;
    
err_vfs:
    unmount_kretprobe_exit();
err_unmount:
    mount_kretprobe_exit();
err_mount:
    return ret;    
}

void bdev_kprobe_module_exit(void)
{
    vfs_write_kretprobe_exit();
    unmount_kretprobe_exit();
    mount_kretprobe_exit();
}

