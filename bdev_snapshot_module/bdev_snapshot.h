#ifndef _BDEVSNAPSHOT_H
#define _BDEVSNAPSHOT_H

/* Nome comodo per i log del modulo */
#define MOD_NAME "bdev_snapshot"

/* Per avere le macro _IO/_IOW sia in kernel che in user-space */
#ifdef __KERNEL__
  #include <linux/ioctl.h>
#else
  #include <sys/ioctl.h>
#endif

/* Magic per le ioctl del nostro dispositivo */
#define BDEVSNAP_IOCTL_MAGIC 's'

/* Argomenti passati dalle ioctl di attivazione/disattivazione */
struct snap_args {
    char dev_name[64];   /* es: "/dev/loop0" */
    char password[64];   /* verrà validata in F3 */
};

/* Codici ioctl esposti all'userspace */
#define SNAP_ACTIVATE    _IOW(BDEVSNAP_IOCTL_MAGIC, 1, struct snap_args)
#define SNAP_DEACTIVATE  _IOW(BDEVSNAP_IOCTL_MAGIC, 2, struct snap_args)

#endif /* _BDEVSNAPSHOT_H */

