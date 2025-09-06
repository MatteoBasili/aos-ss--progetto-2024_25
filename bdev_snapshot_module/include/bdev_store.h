#ifndef _BDEV_STORE_H
#define _BDEV_STORE_H

#include <linux/blkdev.h>
#include <linux/xarray.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/atomic.h>   /* atomic64_t */

/* Directory di base per gli snapshot */
#define SNAP_ROOT_DIR   "/snapshot"
#define SNAP_DIR_MAX    256
#define SNAP_NAME_MAX   64

/* Stato per-device durante il salvataggio dei blocchi */
struct snap_store {
	char      dir[SNAP_DIR_MAX];   /* /snapshot/<dev>_<timestamp> */
	struct xarray saved_map;       /* mappa LBA -> 1 se già salvato */
	struct mutex dir_mutex;        /* protegge la creazione della dir */
	bool      dir_ready;           /* true se la directory è pronta */
	atomic64_t saved_blocks;       /* contatore dei blocchi salvati */
};

/* ==== Inizializzazione globale ==== */
int  bdev_store_global_init(void);
void bdev_store_global_exit(void);

/* ==== Stato per-device ==== */
struct snap_store *snap_store_get_or_create(const char *disk_name, u64 t_ns);

/* ==== API per accodare blocchi da salvare (copia-before-write) ==== */
void snapshot_queue_blocks(struct block_device *bdev,
                           const char *disk_name,
                           sector_t sector,
                           unsigned int nsectors);

#endif /* _BDEV_STORE_H */

