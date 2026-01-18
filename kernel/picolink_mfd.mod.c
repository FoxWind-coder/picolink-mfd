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

KSYMTAB_FUNC(picolink_send_packet, "_gpl", "");
KSYMTAB_FUNC(picolink_transfer, "_gpl", "");
KSYMTAB_DATA(picolink_gpio_driver, "_gpl", "");
KSYMTAB_DATA(picolink_i2c_driver, "_gpl", "");
KSYMTAB_FUNC(picolink_uart_push_data, "_gpl", "");

SYMBOL_CRC(picolink_send_packet, 0x8bf75651, "_gpl");
SYMBOL_CRC(picolink_transfer, 0x8e9de6fd, "_gpl");
SYMBOL_CRC(picolink_gpio_driver, 0xf1d8de1e, "_gpl");
SYMBOL_CRC(picolink_i2c_driver, 0x742e2e82, "_gpl");
SYMBOL_CRC(picolink_uart_push_data, 0x0a6783e1, "_gpl");

static const char ____versions[]
__used __section("__versions") =
	"\x18\x00\x00\x00\x09\x89\x9d\x14"
	"usb_alloc_urb\0\0\0"
	"\x1c\x00\x00\x00\x48\x9f\xdb\x88"
	"__check_object_size\0"
	"\x18\x00\x00\x00\xdf\x66\xdf\x8a"
	"misc_deregister\0"
	"\x18\x00\x00\x00\x47\xfd\x87\x0b"
	"usb_free_urb\0\0\0\0"
	"\x18\x00\x00\x00\xc2\x9c\xc4\x13"
	"_copy_from_user\0"
	"\x24\x00\x00\x00\x0e\xd7\x3a\x4a"
	"wait_for_completion_timeout\0"
	"\x18\x00\x00\x00\x21\xb7\x93\xa1"
	"devm_kmalloc\0\0\0\0"
	"\x24\x00\x00\x00\x5d\x88\x77\x4a"
	"platform_driver_unregister\0\0"
	"\x14\x00\x00\x00\x2f\x7a\x25\xa6"
	"complete\0\0\0\0"
	"\x20\x00\x00\x00\x1d\x25\xa2\xf0"
	"tty_flip_buffer_push\0\0\0\0"
	"\x20\x00\x00\x00\xb5\x41\x87\x60"
	"__init_swait_queue_head\0"
	"\x1c\x00\x00\x00\xed\x32\xb4\xd2"
	"usb_register_driver\0"
	"\x10\x00\x00\x00\x38\xdf\xac\x69"
	"memcpy\0\0"
	"\x10\x00\x00\x00\xba\x0c\x7a\x03"
	"kfree\0\0\0"
	"\x28\x00\x00\x00\x64\xbc\x84\x9f"
	"devm_gpiochip_add_data_with_key\0"
	"\x20\x00\x00\x00\xd8\x94\xd3\x0b"
	"tty_termios_baud_rate\0\0\0"
	"\x1c\x00\x00\x00\x18\x78\x82\xe1"
	"__tty_alloc_driver\0\0"
	"\x20\x00\x00\x00\x56\x75\x21\x9d"
	"tty_standard_install\0\0\0\0"
	"\x20\x00\x00\x00\x3a\x41\x5d\x7c"
	"tty_unregister_driver\0\0\0"
	"\x18\x00\x00\x00\x8c\x89\xd4\xcb"
	"fortify_panic\0\0\0"
	"\x14\x00\x00\x00\xbb\x6d\xfb\xbd"
	"__fentry__\0\0"
	"\x10\x00\x00\x00\x7e\x3a\x2c\x12"
	"_printk\0"
	"\x14\x00\x00\x00\xcb\x61\x3a\xa1"
	"usb_put_dev\0"
	"\x18\x00\x00\x00\x68\x22\x0b\xcf"
	"usb_bulk_msg\0\0\0\0"
	"\x1c\x00\x00\x00\xcb\xf6\xfd\xf0"
	"__stack_chk_fail\0\0\0\0"
	"\x14\x00\x00\x00\x15\xce\x24\xee"
	"usb_get_dev\0"
	"\x18\x00\x00\x00\xc1\x7e\xb2\x67"
	"tty_std_termios\0"
	"\x18\x00\x00\x00\xde\x12\x45\xb3"
	"usb_submit_urb\0\0"
	"\x14\x00\x00\x00\x62\xcf\x79\x60"
	"_dev_info\0\0\0"
	"\x20\x00\x00\x00\x1e\x17\x25\x0e"
	"tty_unregister_device\0\0\0"
	"\x18\x00\x00\x00\x4f\xe2\xee\x1e"
	"i2c_del_adapter\0"
	"\x28\x00\x00\x00\xb3\x1c\xa2\x87"
	"__ubsan_handle_out_of_bounds\0\0\0\0"
	"\x1c\x00\x00\x00\x1c\x51\x5b\x37"
	"gpiochip_get_data\0\0\0"
	"\x14\x00\x00\x00\x2c\x74\x15\xfe"
	"_dev_err\0\0\0\0"
	"\x1c\x00\x00\x00\x63\xa5\x03\x4c"
	"random_kmalloc_seed\0"
	"\x28\x00\x00\x00\x64\x3a\x32\x84"
	"__tty_insert_flip_string_flags\0\0"
	"\x1c\x00\x00\x00\x2a\x71\x1a\x35"
	"tty_port_destroy\0\0\0\0"
	"\x18\x00\x00\x00\x3c\x3b\x87\x7b"
	"tty_port_init\0\0\0"
	"\x18\x00\x00\x00\x83\xe6\x14\xb4"
	"mfd_add_devices\0"
	"\x24\x00\x00\x00\x51\x10\x81\xaa"
	"tty_port_register_device\0\0\0\0"
	"\x10\x00\x00\x00\xe6\x6e\xab\xbc"
	"sscanf\0\0"
	"\x18\x00\x00\x00\xd9\x74\xc0\x23"
	"usb_deregister\0\0"
	"\x18\x00\x00\x00\xee\x60\x97\x53"
	"tty_port_close\0\0"
	"\x18\x00\x00\x00\xfd\xe9\x92\x07"
	"misc_register\0\0\0"
	"\x1c\x00\x00\x00\xca\x39\x82\x5b"
	"__x86_return_thunk\0\0"
	"\x18\x00\x00\x00\x58\x02\xa4\xd4"
	"i2c_add_adapter\0"
	"\x24\x00\x00\x00\xba\x9f\x50\x23"
	"__platform_driver_register\0\0"
	"\x1c\x00\x00\x00\xff\x19\xc6\xfc"
	"tty_register_driver\0"
	"\x18\x00\x00\x00\x63\x54\x25\x5e"
	"usb_kill_urb\0\0\0\0"
	"\x18\x00\x00\x00\xa7\x9f\x06\xfc"
	"tty_port_open\0\0\0"
	"\x1c\x00\x00\x00\xbc\xe5\x5f\xc5"
	"mfd_remove_devices\0\0"
	"\x18\x00\x00\x00\x4c\x48\xc3\xd0"
	"kmalloc_trace\0\0\0"
	"\x10\x00\x00\x00\xf9\x82\xa4\xf9"
	"msleep\0\0"
	"\x1c\x00\x00\x00\x83\xbf\xda\x94"
	"tty_driver_kref_put\0"
	"\x18\x00\x00\x00\x19\x08\xda\x08"
	"kmalloc_caches\0\0"
	"\x18\x00\x00\x00\xeb\x7b\x33\xe1"
	"module_layout\0\0\0"
	"\x00\x00\x00\x00\x00\x00\x00\x00";

MODULE_INFO(depends, "");

MODULE_ALIAS("usb:v1D50p6150d*dc*dsc*dp*ic*isc*ip*in*");

MODULE_INFO(srcversion, "F0C71A1C340494642ACBFE0");
