#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xba995030, "filp_open" },
	{ 0x7088eb1f, "try_module_get" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0x49cd25ed, "alloc_workqueue" },
	{ 0x8d522714, "__rcu_read_lock" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0x9291cd3b, "memdup_user" },
	{ 0x6d93515, "iterate_dir" },
	{ 0x75646747, "class_destroy" },
	{ 0x96848186, "scnprintf" },
	{ 0x69acdf38, "memcpy" },
	{ 0x37a0cba, "kfree" },
	{ 0x30d604dd, "kern_path_create" },
	{ 0x34db050b, "_raw_spin_lock_irqsave" },
	{ 0x253591aa, "path_put" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x89690de2, "vfs_mkdir" },
	{ 0x44e7ec2, "crypto_destroy_tfm" },
	{ 0x65487097, "__x86_indirect_thunk_rax" },
	{ 0x122c3a7e, "_printk" },
	{ 0x8427cc7b, "_raw_spin_lock_irq" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x296695f, "refcount_warn_saturate" },
	{ 0xa916b694, "strnlen" },
	{ 0xc6cbbc89, "capable" },
	{ 0xfca6cab, "module_put" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xec957a9, "cdev_add" },
	{ 0xfe5d4bb2, "sys_tz" },
	{ 0x55a8a611, "__brelse" },
	{ 0xb7c0f443, "sort" },
	{ 0x6091797f, "synchronize_rcu" },
	{ 0x2469810f, "__rcu_read_unlock" },
	{ 0x2e3443fd, "device_create" },
	{ 0x1e6d26a8, "strstr" },
	{ 0x6ca9b86a, "class_create" },
	{ 0x4c03a563, "random_kmalloc_seed" },
	{ 0x8c03d20c, "destroy_workqueue" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x5a921311, "strncmp" },
	{ 0x4b750f53, "_raw_spin_unlock_irq" },
	{ 0x87b77d87, "unregister_kretprobe" },
	{ 0xefb47445, "crypto_shash_update" },
	{ 0x9ec6ca96, "ktime_get_real_ts64" },
	{ 0xbcab6ee6, "sscanf" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x1afee99c, "__bread_gfp" },
	{ 0xd35cce70, "_raw_spin_unlock_irqrestore" },
	{ 0xfb578fc5, "memset" },
	{ 0x13635aed, "kern_path" },
	{ 0xa7e99aae, "kernel_read" },
	{ 0x9166fc03, "__flush_workqueue" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x7bf2339a, "param_ops_string" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x3301914f, "path_get" },
	{ 0xdd64e639, "strscpy" },
	{ 0xa648e561, "__ubsan_handle_shift_out_of_bounds" },
	{ 0x28aa6a67, "call_rcu" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0xbff01dd9, "crypto_shash_final" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0xc310b981, "strnstr" },
	{ 0xc480d49a, "filp_close" },
	{ 0x66b4cc41, "kmemdup" },
	{ 0x19edaabb, "device_destroy" },
	{ 0x2cf56265, "__dynamic_pr_debug" },
	{ 0xfff5afc, "time64_to_tm" },
	{ 0x984866c0, "register_kretprobe" },
	{ 0xb1813a6, "done_path_create" },
	{ 0xd0c3484c, "kmalloc_trace" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x754d539c, "strlen" },
	{ 0x349cba85, "strchr" },
	{ 0x206b82b7, "crypto_alloc_shash" },
	{ 0x22d6de43, "cdev_init" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0x1bff00c8, "kmalloc_caches" },
	{ 0xb1b9cfc9, "cdev_del" },
	{ 0x39ca3952, "kernel_write" },
	{ 0x8ae2759e, "d_path" },
	{ 0xe2fd41e5, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "8478E72A6E73D70C7F48AA0");
