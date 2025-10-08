#include <linux/file.h> 
#include <linux/module.h>

#include "bdev_list.h"
#include "snap_store.h"

/* ============================================================
 * Global snapshot device list
 * ============================================================ */
static LIST_HEAD(snap_dev_list);
static DEFINE_MUTEX(snap_dev_mutex);

/* ============================================================
 * Internal helpers
 * ============================================================ */

/* kref release callback for snap_device */
static void snap_device_release(struct kref *kref)
{
    struct snap_device *dev = container_of(kref, struct snap_device, ref);
    
    kfree(dev);
}


/* Find device by name (RCU read-only) */
static struct snap_device *_find_snap_device_rcu(const char *dev_name)
{
    struct snap_device *dev;
    
    list_for_each_entry_rcu(dev, &snap_dev_list, list) {
        if (strncmp(dev->dev_name, dev_name, DEV_NAME_LEN_MAX) == 0)
            return dev;
    }
    return NULL;
}

static void cleanup_device_wq_if_needed(struct snap_device *dev)
{
    if (!dev)
        return;

    if (!dev->enabled && !dev->mounted && dev->wq) {
        flush_workqueue(dev->wq);
        destroy_workqueue(dev->wq);
        dev->wq = NULL;
    }
}

/* Internal: remove device (snap_dev_mutex must already be held) */
static void __remove_device_if_disabled(struct snap_device *dev)
{
    bool drop = false;

    mutex_lock(&dev->lock);
    if (!dev->enabled && !dev->mounted)
        drop = true;
    mutex_unlock(&dev->lock);

    if (drop) {
        list_del_rcu(&dev->list);
        synchronize_rcu();
        snap_device_put(dev);
    }
}

/* Public safe wrapper: acquires snap_dev_mutex */
static void remove_device_if_disabled(struct snap_device *dev)
{
    mutex_lock(&snap_dev_mutex);
    __remove_device_if_disabled(dev);
    mutex_unlock(&snap_dev_mutex);
}

/* ============================================================
 * Device reference helpers
 * ============================================================ */

/* Increment device reference */
void snap_device_get(struct snap_device *dev)
{
    if (dev)
        kref_get(&dev->ref);
}

/* Release device reference */
void snap_device_put(struct snap_device *dev)
{
    if (dev)
        kref_put(&dev->ref, snap_device_release);
}

/* Find a device by name and increment reference */
struct snap_device *snap_find_device_get(const char *dev_name)
{
    struct snap_device *dev = NULL;

    if (!dev_name)
        return NULL;

    rcu_read_lock();
    dev = _find_snap_device_rcu(dev_name);
    if (dev && !kref_get_unless_zero(&dev->ref))
        dev = NULL;
    rcu_read_unlock();

    return dev;
}

/* ============================================================
 * Device list management
 * ============================================================ */

/* Add new device or enable existing one */
int add_or_enable_snap_device(const char *dev_name)
{
    struct snap_device *dev;
    int ret = 0;
    char wq_name[DEV_NAME_LEN_MAX + 8];

    mutex_lock(&snap_dev_mutex);

    /* Check if device already exists */
    list_for_each_entry(dev, &snap_dev_list, list) {
        mutex_lock(&dev->lock);
        if (strncmp(dev->dev_name, dev_name, DEV_NAME_LEN_MAX) == 0) {
            if (dev->enabled) {
                ret = 1;  /* already enabled */
            } else {
                dev->enabled = true;
            }
            mutex_unlock(&dev->lock);
            goto out_unlock;
        }
        mutex_unlock(&dev->lock);
    }

    /* Allocate new device */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        ret = -ENOMEM;
        goto out_unlock;
    }

    strscpy(dev->dev_name, dev_name, sizeof(dev->dev_name));

    snprintf(wq_name, sizeof(wq_name), "%s_wq", dev->dev_name);
    dev->wq = alloc_ordered_workqueue(wq_name, WQ_MEM_RECLAIM);
    if (!dev->wq) {
        kfree(dev);
        ret = -ENOMEM;
        goto out_unlock;
    }
    
    dev->enabled = true;
    dev->mounted = false;
    dev->saved_bitmap = NULL;
    
    mutex_init(&dev->lock);
    spin_lock_init(&dev->spin_lock);
    kref_init(&dev->ref);

    list_add_rcu(&dev->list, &snap_dev_list);

out_unlock:
    mutex_unlock(&snap_dev_mutex);
    return ret;
}

/* Disable snapshot device */
int disable_snap_device(const char *dev_name)
{
    struct snap_device *dev;
    int ret = -ENOENT;

    mutex_lock(&snap_dev_mutex);
    list_for_each_entry(dev, &snap_dev_list, list) {
        mutex_lock(&dev->lock);
        if (strncmp(dev->dev_name, dev_name, DEV_NAME_LEN_MAX) == 0) {
            if (!dev->enabled) {
                ret = 1; /* already disabled */
            } else {
                dev->enabled = false;
                mutex_unlock(&dev->lock);
                
                __remove_device_if_disabled(dev);
                ret = 0;
                goto out_unlock;
            }
            mutex_unlock(&dev->lock);
            goto out_unlock;
        }
        mutex_unlock(&dev->lock);
    }
out_unlock:
    mutex_unlock(&snap_dev_mutex);
    return ret;
}

/* ============================================================
 * Device mount/unmount
 * ============================================================ */

/* Mark device as mounted */
int snapdev_mark_mounted(struct snap_device *dev)
{
    unsigned long flags;
    int ret = 0;

    if (!dev)
        return -EINVAL;

    spin_lock_irqsave(&dev->spin_lock, flags);
    if (!dev->enabled) {
        ret = -EPERM;
    } else if (dev->mounted) {
        ret = -EBUSY;
    } else {
        dev->mounted = true;
        ktime_get_real_ts64(&dev->mount_time);
        ret = 0;
    }
    
    spin_unlock_irqrestore(&dev->spin_lock, flags);
    
    return ret;
}

/* Heavy mount work */
int snapdev_do_mount_work(struct snap_device *dev)
{
    int ret = 0;

    if (!dev)
        return -EINVAL;

    mutex_lock(&dev->lock);

    /* Double-check: must be marked mounted */
    if (!dev->mounted) {
        ret = -EINVAL;
        goto out_unlock;
    }

    /* Open snapshot directory + metadata.json */
    ret = open_snapshot(dev);
    if (ret < 0)
        goto fail_unmount;

    /* Allocate bitmap */
    kfree(dev->saved_bitmap);
    dev->saved_bitmap = kzalloc(BITS_TO_LONGS(dev->num_blocks) * sizeof(long), GFP_KERNEL);
    if (!dev->saved_bitmap) {
        ret = -ENOMEM;
        goto fail_unmount;
    }

    goto out_unlock;

fail_unmount:
    /* Rollback: clear mounted flag */
    spin_lock_irq(&dev->spin_lock);
    dev->mounted = false;
    spin_unlock_irq(&dev->spin_lock);

out_unlock:
    mutex_unlock(&dev->lock);
    return ret;
}

/* Mark device as unmounted */
int snapdev_mark_unmounted(struct snap_device *dev)
{
    unsigned long flags;
    int ret = 0;

    if (!dev)
        return -EINVAL;

    spin_lock_irqsave(&dev->spin_lock, flags);
    if (dev->mounted) {
        dev->mounted = false;
        ret = 0;
    } else {
        ret = -EINVAL;
    }
    
    spin_unlock_irqrestore(&dev->spin_lock, flags);

    return ret;
}

/* Internal: heavy unmount work */
static int __snapdev_do_unmount_work(struct snap_device *dev)
{
    int ret = 0;

    if (!dev)
        return -EINVAL;

    mutex_lock(&dev->lock);

    if (dev->mounted) {
        ret = -EINVAL;
        goto out_unlock;
    }

    cleanup_device_wq_if_needed(dev);
    close_snapshot(dev);

    kfree(dev->saved_bitmap);
    dev->saved_bitmap = NULL;

out_unlock:
    mutex_unlock(&dev->lock);
    return ret;
}

/* Heavy unmount work */
int snapdev_do_unmount_work(struct snap_device *dev)
{
    int ret = __snapdev_do_unmount_work(dev);
    
    if (ret == 0) {
        remove_device_if_disabled(dev);
    }
    return ret;
}

/* ------------------------------------------------------ */

/* Get saved_bitmap pointer with spinlock protection */
unsigned long *snapdev_get_saved_bitmap(struct snap_device *dev)
{
    unsigned long *bitmap = NULL;
    unsigned long flags;

    if (!dev)
        return NULL;

    spin_lock_irqsave(&dev->spin_lock, flags);
    if (dev->saved_bitmap) {
        bitmap = dev->saved_bitmap;
    }
    spin_unlock_irqrestore(&dev->spin_lock, flags);

    return bitmap;
}

/* Check if device is mounted */
bool snapdev_is_mounted(struct snap_device *dev)
{
    bool mounted = false;
    unsigned long flags;

    if (!dev)
        return false;

    spin_lock_irqsave(&dev->spin_lock, flags);
    mounted = dev->mounted;
    spin_unlock_irqrestore(&dev->spin_lock, flags);

    return mounted;
}

/* ============================================================
 * Cleanup
 * ============================================================ */

/* Clear all devices */
void clear_snap_devices(void)
{
    struct snap_device *dev, *tmp;

    mutex_lock(&snap_dev_mutex);
    list_for_each_entry_safe(dev, tmp, &snap_dev_list, list) {
        mutex_lock(&dev->lock);
        dev->enabled = false;  // force the removal
        mutex_unlock(&dev->lock);
        
        snapdev_mark_unmounted(dev);
        __snapdev_do_unmount_work(dev);
        
        __remove_device_if_disabled(dev);
    }
    mutex_unlock(&snap_dev_mutex);
    synchronize_rcu();
}

