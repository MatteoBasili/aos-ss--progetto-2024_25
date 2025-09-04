#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/ktime.h>

#include "bdev_snapshot.h"
#include "bdev_store.h"

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

/* Normalizza il nome: se inizia con "/dev/", ritorna solo la parte finale */
static const char *normalize_name(const char *dev_name)
{
	if (!dev_name)
		return dev_name;
	if (strncmp(dev_name, "/dev/", 5) == 0)
		return dev_name + 5;
	return dev_name;
}

int add_snap_device(const char *dev_name)
{
	struct snap_device *dev;
	int ret = 0;
	const char *basename = normalize_name(dev_name);

	if (!basename)
		return -EINVAL;

	mutex_lock(&snap_dev_mutex);
	if (find_snap_device(basename)) {
		ret = -EEXIST;
		goto out;
	}
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto out;
	}
	strscpy(dev->dev_name, basename, sizeof(dev->dev_name));
	dev->started = false;
	dev->first_write_ns = 0;
	list_add(&dev->list, &snap_dev_list);
	pr_info("%s: snapshot activated for device '%s'\n", MOD_NAME, dev->dev_name);
out:
	mutex_unlock(&snap_dev_mutex);
	return ret;
}

int remove_snap_device(const char *dev_name)
{
	struct snap_device *dev;
	int ret = -ENOENT;
	const char *basename = normalize_name(dev_name);

	if (!basename)
		return -EINVAL;

	mutex_lock(&snap_dev_mutex);
	dev = find_snap_device(basename);
	if (dev) {
		list_del(&dev->list);
		pr_info("%s: snapshot deactivated for device '%s'\n", MOD_NAME, dev->dev_name);
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
		pr_info("%s: clearing device '%s'\n", MOD_NAME, dev->dev_name);
		kfree(dev);
	}
	mutex_unlock(&snap_dev_mutex);
}

/* --- Nuove API per F6/F7 --- */
/* Controlla se il nome del "disk" (es: loop0, sda) è presente nella lista */
bool snapdev_is_active_name(const char *disk_name)
{
	bool found = false;
	struct snap_device *dev;

	if (!disk_name)
		return false;

	mutex_lock(&snap_dev_mutex);
	list_for_each_entry(dev, &snap_dev_list, list) {
		if (strcmp(dev->dev_name, disk_name) == 0) {
			found = true;
			break;
		}
	}
	mutex_unlock(&snap_dev_mutex);
	return found;
}

/* Marca come "started" (prima write intercettata) e salva timestamp se ancora non set.
 * Inoltre prepara lo store F7 (snap_store_get_or_create) per creare la directory alla prima write.
 */
void snapdev_mark_started(const char *disk_name, u64 t_ns)
{
	struct snap_device *dev;

	if (!disk_name)
		return;

	mutex_lock(&snap_dev_mutex);
	list_for_each_entry(dev, &snap_dev_list, list) {
		if (strcmp(dev->dev_name, disk_name) == 0) {
			if (!dev->started) {
				dev->started = true;
				dev->first_write_ns = t_ns;
				pr_info("%s: first write detected on '%s' at %llu ns\n",
				        MOD_NAME, dev->dev_name, (unsigned long long)t_ns);
				/* Prepara struttura F7 (crea store + directory su demand) */
				snap_store_get_or_create(disk_name, t_ns);
			}
			break;
		}
	}
	mutex_unlock(&snap_dev_mutex);
}

