#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/string.h>

#include "bdev_snapshot.h"

/* Lista dispositivi snapshot */
static LIST_HEAD(snap_dev_list);
static DEFINE_MUTEX(snap_dev_mutex);

static struct snap_device *find_snap_device(const char *dev_name)
{
    struct snap_device *dev;
    list_for_each_entry(dev, &snap_dev_list, list) {
        if (strcmp(dev->dev_name, dev_name) == 0)
            return dev;
    }
    return NULL;
}

int add_snap_device(const char *dev_name)
{
    struct snap_device *dev;
    int ret = 0;

    mutex_lock(&snap_dev_mutex);
    if (find_snap_device(dev_name)) {
        ret = -EEXIST;
        goto out;
    }

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        ret = -ENOMEM;
        goto out;
    }

    strscpy(dev->dev_name, dev_name, sizeof(dev->dev_name));
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
    dev = find_snap_device(dev_name);
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

