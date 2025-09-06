#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include "bdev_snapshot.h"

static LIST_HEAD(snap_dev_list);
static DEFINE_MUTEX(snap_dev_mutex);

static struct snap_device *_find_snap_device(const char *dev_name)
{
    struct snap_device *dev;
    list_for_each_entry(dev, &snap_dev_list, list) {
        if (strcmp(dev->dev_name, dev_name) == 0)
            return dev;
    }
    return NULL;
}

struct snap_device *snap_find_device(const char *disk_name)
{
    struct snap_device *dev;
    mutex_lock(&snap_dev_mutex);
    dev = _find_snap_device(disk_name);
    mutex_unlock(&snap_dev_mutex);
    return dev;
}

int add_snap_device(const char *dev_name)
{
    struct snap_device *dev;
    int ret = 0;

    if (!dev_name)
        return -EINVAL;

    mutex_lock(&snap_dev_mutex);
    if (_find_snap_device(dev_name)) {
        ret = -EEXIST;
        goto out;
    }

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        ret = -ENOMEM;
        goto out;
    }
    strscpy(dev->dev_name, dev_name, sizeof(dev->dev_name));
    dev->started = false;
    dev->first_write_ns = 0;
    list_add(&dev->list, &snap_dev_list);
out:
    mutex_unlock(&snap_dev_mutex);
    return ret;
}

int remove_snap_device(const char *dev_name)
{
    struct snap_device *dev;
    int ret = -ENOENT;

    mutex_lock(&snap_dev_mutex);
    dev = _find_snap_device(dev_name);
    if (dev) {
        list_del(&dev->list);
        kfree(dev);
        ret = 0;
    }
    mutex_unlock(&snap_dev_mutex);
    return ret;
}

void clear_snap_devices(void)
{
    struct snap_device *dev, *tmp;
    mutex_lock(&snap_dev_mutex);
    list_for_each_entry_safe(dev, tmp, &snap_dev_list, list) {
        list_del(&dev->list);
        kfree(dev);
    }
    mutex_unlock(&snap_dev_mutex);
}

bool snapdev_is_active_name(const char *disk_name)
{
    bool found = false;
    struct snap_device *dev;

    mutex_lock(&snap_dev_mutex);
    dev = _find_snap_device(disk_name);
    found = dev != NULL;
    mutex_unlock(&snap_dev_mutex);
    return found;
}

void snapdev_mark_started(const char *disk_name, u64 t_ns)
{
    struct snap_device *dev;
    mutex_lock(&snap_dev_mutex);
    dev = _find_snap_device(disk_name);
    if (dev && !dev->started) {
        dev->started = true;
        dev->first_write_ns = t_ns;
        //snap_store_get_or_create(disk_name, t_ns);
    }
    mutex_unlock(&snap_dev_mutex);
}

