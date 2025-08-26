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
	{ 0x37a0cba, "kfree" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x49cd25ed, "alloc_workqueue" },
	{ 0x3f66a26e, "register_kprobe" },
	{ 0xf4cc6a9a, "__register_chrdev" },
	{ 0xc74fa6a1, "class_create" },
	{ 0xf4ee463a, "device_create" },
	{ 0x6bc3fbc0, "__unregister_chrdev" },
	{ 0xbb10e61d, "unregister_kprobe" },
	{ 0x1fc34ff, "class_destroy" },
	{ 0x9166fc03, "__flush_workqueue" },
	{ 0x8c03d20c, "destroy_workqueue" },
	{ 0x3c06c15f, "device_destroy" },
	{ 0x9ec6ca96, "ktime_get_real_ts64" },
	{ 0xa7eedcc4, "call_usermodehelper" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x9166fada, "strncpy" },
	{ 0x71ba2490, "pcpu_hot" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0x4c03a563, "random_kmalloc_seed" },
	{ 0x37a99944, "kmalloc_caches" },
	{ 0x22e14f04, "kmalloc_trace" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0x69acdf38, "memcpy" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0x48d88a2c, "__SCT__preempt_schedule" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x656e4a6e, "snprintf" },
	{ 0x64f48f75, "filp_open" },
	{ 0xb2c57545, "kernel_write" },
	{ 0xeaa78587, "filp_close" },
	{ 0x122c3a7e, "_printk" },
	{ 0xc6227e48, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "7F42C9C508ACA17152FA721");
