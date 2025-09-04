/*
 * bdev_kprobe.c
 *
 * Kprobe minimale per intercettare scritture a livello blocco.
 * Si aggancia a "submit_bio".
 *
 * NOTE:
 *  - Il pre_handler è estremamente leggero: verifica tipo di bio,
 *    ottiene disk_name e controlla se il device è attivo.
 *  - Non vengono effettuate allocazioni, IO o altre operazioni
 *    bloccanti qui. Tutto il salvataggio viene spostato in F7/F8
 *    tramite snapshot_queue_blocks().
 */

#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/ktime.h>
#include <linux/printk.h>
#include <linux/version.h>

#include "bdev_snapshot.h"
#include "bdev_store.h"

/* kprobe struct */
static struct kprobe kp_submit;

/* Macro portabile per estrarre il 1° arg (bio*) dai pt_regs su x86-64. */
#ifndef PT_REGS_PARM1
# if defined(CONFIG_X86_64)
#  define PT_REGS_PARM1(x) ((void *)((x)->di))
# else
#  define PT_REGS_PARM1(x) NULL
# endif
#endif

/* Controlla se il bio è una scrittura */
static int is_write_bio(const struct bio *bio)
{
	if (!bio)
		return 0;

	switch (bio_op(bio)) {
	case REQ_OP_WRITE:
	case REQ_OP_WRITE_ZEROES:
	case REQ_OP_DISCARD:
		/* opzionale: conta come “write” logica */
		return 1;
	default:
		return 0;
	}
}

/* pre_handler: viene eseguito prima della chiamata a submit_bio() */
static int submit_bio_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct bio *bio = (struct bio *)PT_REGS_PARM1(regs);
	struct block_device *bdev;
	const char *disk_name;
	sector_t first_sector;
	unsigned int nsectors;

	if (!bio)
		return 0;

	/* Solo write */
	if (!is_write_bio(bio))
		return 0;

	bdev = bio->bi_bdev;
	if (!bdev || !bdev->bd_disk)
		return 0;

	disk_name = bdev->bd_disk->disk_name; /* es: "loop0", "sda" */
	if (!disk_name)
		return 0;

	/* Controlla se questo disk è monitorato */
	if (!snapdev_is_active_name(disk_name))
		return 0;

	/* Marca la prima scrittura (timestamp) */
	snapdev_mark_started(disk_name, ktime_get_ns());

	first_sector = bio->bi_iter.bi_sector;
	nsectors = bio_sectors(bio);

	pr_debug("%s: intercepted WRITE on %s sector=%llu nsectors=%u\n",
	         MOD_NAME, disk_name, (unsigned long long)first_sector, nsectors);

	/*
	 * Accoda salvataggio dei blocchi (best-effort) su workqueue.
	 * snapshot_queue_blocks si occupa di prendere una ref sul bdev
	 * e creare il lavoro in modo safe (GFP_ATOMIC).
	 */
	snapshot_queue_blocks(bdev, disk_name, first_sector, nsectors);

	return 0;
}

int bdev_kprobe_init(void)
{
	int ret;

	kp_submit.symbol_name = "submit_bio";
	kp_submit.pre_handler = submit_bio_pre;

	ret = register_kprobe(&kp_submit);
	if (ret) {
		pr_err("%s: register_kprobe failed for %s: %d\n",
		       MOD_NAME, kp_submit.symbol_name, ret);
		return ret;
	}

	pr_info("%s: kprobe registered on %s\n", MOD_NAME, kp_submit.symbol_name);
	return 0;
}

void bdev_kprobe_exit(void)
{
	unregister_kprobe(&kp_submit);
	pr_info("%s: kprobe unregistered\n", MOD_NAME);
}

