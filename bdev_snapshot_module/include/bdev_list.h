#ifndef _BDEV_LIST_H
#define _BDEV_LIST_H

#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/types.h>

#define DEV_NAME_LEN_MAX  64
#define SNAP_NAME_MAX     64

/* API pubbliche per snapshot devices */
int add_snap_device(const char *dev_name);
int remove_snap_device(const char *dev_name);
void clear_snap_devices(void);

/* Controlla se il disk è attivo */
bool snapdev_is_active_name(const char *disk_name);
/* Marca il device come "started" e salva timestamp */
void snapdev_mark_started(const char *disk_name, u64 t_ns);
/* Cerca device e ritorna puntatore */
struct snap_device *snap_find_device(const char *disk_name);

#endif

