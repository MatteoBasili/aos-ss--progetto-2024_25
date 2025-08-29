#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>      /* For LINUX_VERSION_CODE */


#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
#error "This module requires at least the 6.3.0 kernel version."
#endif


static int bdevsnapshot_init(void)
{
    printk("Loaded block-device snapshot module\n");
    return 0;
}

static void bdevsnapshot_exit(void)
{
    printk("Removed block-device snapshot module\n");
}

module_init(bdevsnapshot_init);
module_exit(bdevsnapshot_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matteo Basili <matteo.basili@students.uniroma2.eu>");
MODULE_DESCRIPTION("Snapshot service for block devices hosting file systems");

