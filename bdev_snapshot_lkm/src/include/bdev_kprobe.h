#ifndef _BDEV_KPROBE_H
#define _BDEV_KPROBE_H

#include "uapi/bdev_snapshot.h"

/* Portable macro to extract first argument from pt_regs on x86-64 */
#ifndef PT_REGS_PARM1
# if defined(CONFIG_X86_64)
#  define PT_REGS_PARM1(x) ((void *)((x)->di))
# else
#  define PT_REGS_PARM1(x) NULL
# endif
#endif

/* Portable macro to extract third argument from pt_regs on x86-64 */
#ifndef PT_REGS_PARM3
# if defined(CONFIG_X86_64)
#  define PT_REGS_PARM3(x) ((void *)((x)->dx))
# else
#  define PT_REGS_PARM3(x) NULL
# endif
#endif

/* Portable macro to extract fourth argument from pt_regs on x86-64 */
#ifndef PT_REGS_PARM4
# if defined(CONFIG_X86_64)
#  define PT_REGS_PARM4(x) ((void *)((x)->cx))
# else
#  define PT_REGS_PARM4(x) NULL
# endif
#endif

/* Metadata for unmount kretprobe */
struct umount_kretprobe_metadata {
    char dev_name[DEV_NAME_LEN_MAX];
};

struct singlefilefs_write_kretprobe_metadata {
    struct snap_pending_block *pending_blocks;
    loff_t original_offset;
    loff_t original_inode_size; 
    struct inode *inode;
};

/* ================= Workqueue Structures ================= */
struct mount_work {
    struct work_struct work;
    char dev_name[DEV_NAME_LEN_MAX];
};

struct unmount_work {
    struct work_struct work;
    char dev_name[DEV_NAME_LEN_MAX];
    long unmount_ret;
};

int bdev_kprobe_module_init(void);
void bdev_kprobe_module_exit(void);

#endif

