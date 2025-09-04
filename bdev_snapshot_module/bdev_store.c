#include <linux/module.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/xarray.h>
#include <linux/uaccess.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/mount.h>   /* per mnt_idmap() */
#include "bdev_snapshot.h"
#include "bdev_store.h"

/* Workqueue globale */
static struct workqueue_struct *snap_wq;

/* Stato per-device */
struct perdev {
	struct snap_store store;
	struct list_head list;
	char disk_name[SNAP_NAME_MAX];
};
static LIST_HEAD(perdev_list);
static DEFINE_MUTEX(perdev_lock);

/* Worker di copia-before-write */
struct copy_work {
	struct work_struct work;
	dev_t dev;                        /* device number del bdev */
	char disk_name[SNAP_NAME_MAX];
	sector_t sector;
	unsigned int nsectors;
};

/* ==== Funzioni di supporto ==== */

static int ensure_root_dir(void)
{
	struct path p;
	int err = kern_path(SNAP_ROOT_DIR, LOOKUP_DIRECTORY, &p);
	if (!err) {
		path_put(&p);
		return 0;
	}

	/* crea /snapshot */
	{
		struct path parent;
		struct dentry *dentry;
		umode_t mode = 0700;

		dentry = kern_path_create(AT_FDCWD, SNAP_ROOT_DIR, &parent, 0);
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);

		err = vfs_mkdir(mnt_idmap(parent.mnt), d_inode(parent.dentry), dentry, mode);
		done_path_create(&parent, dentry);
		path_put(&parent);

		if (err && err != -EEXIST)
			return err;
	}
	return 0;
}

static int ensure_dirs(struct snap_store *st, const char *disk_name, u64 t_ns)
{
	int ret;
	if (st->dir_ready)
		return 0;

	mutex_lock(&st->dir_mutex);
	if (st->dir_ready) {
		mutex_unlock(&st->dir_mutex);
		return 0;
	}

	ret = ensure_root_dir();
	if (ret) {
		mutex_unlock(&st->dir_mutex);
		return ret;
	}

	u64 secs = ktime_get_real_seconds();
	if (!secs)
		secs = div_u64(t_ns, 1000000000ULL);

	snprintf(st->dir, sizeof(st->dir), SNAP_ROOT_DIR"/%s_%llu",
	         disk_name, (unsigned long long)secs);

	/* crea la sottodirectory se non esiste */
	{
		struct path parent;
		struct dentry *dentry;
		umode_t mode = 0700;
		ret = kern_path(SNAP_ROOT_DIR, LOOKUP_DIRECTORY, &parent);
		if (ret) {
			mutex_unlock(&st->dir_mutex);
			return ret;
		}

		dentry = kern_path_create(AT_FDCWD, st->dir, &parent, 0);
		if (IS_ERR(dentry)) {
			path_put(&parent);
			mutex_unlock(&st->dir_mutex);
			return PTR_ERR(dentry);
		}

		ret = vfs_mkdir(mnt_idmap(parent.mnt), d_inode(parent.dentry), dentry, mode);
		done_path_create(&parent, dentry);
		path_put(&parent);

		if (ret && ret != -EEXIST) {
			mutex_unlock(&st->dir_mutex);
			return ret;
		}
	}

	st->dir_ready = true;
	mutex_unlock(&st->dir_mutex);
	return 0;
}

static int save_block_file(struct snap_store *st, sector_t lba, const void *buf, size_t len)
{
	char path[SNAP_DIR_MAX + 64];
	struct file *filp;
	loff_t pos = 0;
	int ret;

	snprintf(path, sizeof(path), "%s/block_%llu.bin",
	         st->dir, (unsigned long long)lba);

	filp = filp_open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (IS_ERR(filp))
		return PTR_ERR(filp);

	ret = kernel_write(filp, buf, len, &pos);
	filp_close(filp, NULL);
	return ret < 0 ? ret : 0;
}

static int read_sectors(struct block_device *bdev, sector_t start, unsigned int nsectors, void *buf)
{
	struct bio *bio = bio_alloc(bdev, 1, REQ_OP_READ | REQ_SYNC, GFP_KERNEL);
	struct page *page;
	int ret;

	if (!bio)
		return -ENOMEM;

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		bio_put(bio);
		return -ENOMEM;
	}

	const unsigned int bytes_per_sector = 512;
	unsigned int total_bytes = nsectors * bytes_per_sector;
	unsigned int done = 0;

	while (done < total_bytes) {
		unsigned int chunk = min_t(unsigned int, total_bytes - done, PAGE_SIZE);
		sector_t this_lba = start + done / bytes_per_sector;

		bio_reset(bio, bdev, REQ_OP_READ | REQ_SYNC);
		if (bio_add_page(bio, page, chunk, 0) != chunk) {
			ret = -EIO;
			goto out;
		}
		bio->bi_iter.bi_sector = this_lba;

		ret = submit_bio_wait(bio);
		if (ret)
			goto out;

		memcpy(buf + done, page_address(page), chunk);
		done += chunk;
	}

	ret = 0;
out:
	__free_page(page);
	bio_put(bio);
	return ret;
}

/* Worker di copia-before-write (versione migliorata):
 * - legge fino a 'nsectors' settori in un unico buffer (limitato)
 * - per ogni settore controlla saved_map e salva il file block_<lba>.bin
 * - minimizza le chiamate bio multiple
 *
 * Nota: le allocazioni qui sono in GFP_KERNEL (safety: siamo in workqueue)
 */

#define BYTES_PER_SECTOR 512
/* Limite pratico per non occupare troppa memoria nel worker:
 * es: 4096 settori * 512 = 2 MiB */
#define MAX_SECTORS_PER_WORK (4096)

static void do_copy_work(struct work_struct *w)
{
    struct copy_work *cw = container_of(w, struct copy_work, work);
    struct snap_store *st = NULL;
    sector_t lba = cw->sector;
    unsigned int n = cw->nsectors;
    int ret = 0;
    struct bdev_handle *handle = NULL;
    struct block_device *bdev = NULL;
    void *buf = NULL;
    unsigned int to_read;
    unsigned int bytes;
    unsigned int i;

    /* Sanity bounds */
    if (!n) {
        pr_warn("%s: copy_work: zero nsectors\n", MOD_NAME);
        goto out;
    }

    /* Apertura bdev (refcount safe) */
    handle = bdev_open_by_dev(cw->dev, FMODE_READ, NULL, NULL);
    if (IS_ERR(handle)) {
        pr_warn("%s: bdev_open_by_dev failed for dev=%u:%u\n",
                MOD_NAME, MAJOR(cw->dev), MINOR(cw->dev));
        handle = NULL;
        goto out;
    }
    bdev = handle->bdev;
    if (!bdev) {
        pr_warn("%s: handle->bdev is NULL for dev=%u:%u\n",
                MOD_NAME, MAJOR(cw->dev), MINOR(cw->dev));
        goto out;
    }

    /* Ottieni o crea lo store del device */
    st = snap_store_get_or_create(cw->disk_name, ktime_get_ns());
    if (!st)
        goto out;

    /* Assicura che la directory dello snapshot esista */
    ret = ensure_dirs(st, cw->disk_name, ktime_get_ns());
    if (ret) {
        pr_warn("%s: ensure_dirs failed: %d\n", MOD_NAME, ret);
        goto out;
    }

    /* Limitiamo la lettura per lavoro per non allocare troppa memoria */
    to_read = min_t(unsigned int, n, MAX_SECTORS_PER_WORK);
    bytes = to_read * BYTES_PER_SECTOR;

    buf = vmalloc(bytes);
    if (!buf) {
        pr_warn("%s: vmalloc(%u) failed\n", MOD_NAME, bytes);
        goto out;
    }

    /* Leggi l'intervallo: read_sectors già supporta nsectors>1 */
    ret = read_sectors(bdev, lba, to_read, buf);
    if (ret) {
        pr_warn("%s: read_sectors failed lba=%llu n=%u ret=%d\n",
                MOD_NAME, (unsigned long long)lba, to_read, ret);
        goto out_free;
    }

    /* Per ogni settore nel buffer:
     * - se non salvato (saved_map), memorizza il file block_<lba+i>.bin
     * - segnalo con xa_store per non salvarlo più volte
     *
     * Nota: xa_store può fallire se c'è concorrenza; in quel caso
     * saltiamo il settore (best-effort).
     */
    for (i = 0; i < to_read; i++) {
        sector_t this_lba = lba + i;
        void *sector_ptr = buf + (i * BYTES_PER_SECTOR);

        /* Se già salvato, salta */
        if (xa_load(&st->saved_map, this_lba))
            continue;

        /* marca come salvato (XA_ZERO_ENTRY è un segnaposto) */
        if (xa_store(&st->saved_map, this_lba, XA_ZERO_ENTRY, GFP_KERNEL)) {
            /* store fallito: possibile race, salta */
            continue;
        }

        ret = save_block_file(st, this_lba, sector_ptr, BYTES_PER_SECTOR);
        if (ret) {
            pr_warn("%s: save lba=%llu on %s failed: %d\n",
                    MOD_NAME, (unsigned long long)this_lba, cw->disk_name, ret);
            /* In caso di fallimento reale, possiamo rimuovere l'entry dalla xarray
             * così che future writes possano ritentare; opzionale:
             */
            xa_erase(&st->saved_map, this_lba);
        } else {
            atomic64_inc(&st->saved_blocks);
        }
    }

    /*
     * Se ci sono ancora più settori oltre 'to_read', generiamo e
     * accodiamo un nuovo lavoro per il resto.
     */
    if (n > to_read) {
        struct copy_work *cw2 = kzalloc(sizeof(*cw2), GFP_KERNEL);
        if (cw2) {
            INIT_WORK(&cw2->work, do_copy_work);
            strscpy(cw2->disk_name, cw->disk_name, sizeof(cw2->disk_name));
            cw2->dev = cw->dev;
            cw2->sector = lba + to_read;
            cw2->nsectors = n - to_read;
            queue_work(snap_wq, &cw2->work);
            /* nota: se allocazione fallisce, i settori rimanenti saranno persi (best-effort).
             * Potresti loggare qui per debugging.
             */
        } else {
            pr_warn("%s: failed to alloc cw2 for remaining sectors\n", MOD_NAME);
        }
    }

out_free:
    vfree(buf);
out:
    if (handle)
        bdev_release(handle);
    kfree(cw);
}

/* API per il kprobe: accoda lavoro */
void snapshot_queue_blocks(struct block_device *bdev,
                           const char *disk_name,
                           sector_t sector,
                           unsigned int nsectors)
{
	struct copy_work *cw;
	if (!snap_wq || !disk_name || !bdev || !nsectors)
		return;

	cw = kzalloc(sizeof(*cw), GFP_ATOMIC);
	if (!cw)
		return;

	INIT_WORK(&cw->work, do_copy_work);
	strscpy(cw->disk_name, disk_name, sizeof(cw->disk_name));
	cw->dev = bdev->bd_dev;
	cw->sector = sector;
	cw->nsectors = nsectors;

	queue_work(snap_wq, &cw->work);
}

/* ===== per-device registry ===== */
struct snap_store *snap_store_get_or_create(const char *disk_name, u64 t_ns)
{
	struct perdev *pd;
	mutex_lock(&perdev_lock);
	list_for_each_entry(pd, &perdev_list, list) {
		if (strcmp(pd->disk_name, disk_name) == 0) {
			mutex_unlock(&perdev_lock);
			return &pd->store;
		}
	}

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		mutex_unlock(&perdev_lock);
		return NULL;
	}
	strscpy(pd->disk_name, disk_name, sizeof(pd->disk_name));
	xa_init(&pd->store.saved_map);
	mutex_init(&pd->store.dir_mutex);
	pd->store.dir_ready = false;
	atomic64_set(&pd->store.saved_blocks, 0);
	list_add(&pd->list, &perdev_list);
	mutex_unlock(&perdev_lock);
	return &pd->store;
}

/* ===== init/exit globali ===== */
int bdev_store_global_init(void)
{
	snap_wq = alloc_workqueue(MOD_NAME "_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	return snap_wq ? 0 : -ENOMEM;
}

void bdev_store_global_exit(void)
{
	struct perdev *pd, *tmp;
	if (snap_wq) {
		flush_workqueue(snap_wq);
		destroy_workqueue(snap_wq);
		snap_wq = NULL;
	}

	mutex_lock(&perdev_lock);
	list_for_each_entry_safe(pd, tmp, &perdev_list, list) {
		list_del(&pd->list);
		xa_destroy(&pd->store.saved_map);
		kfree(pd);
	}
	mutex_unlock(&perdev_lock);
}

