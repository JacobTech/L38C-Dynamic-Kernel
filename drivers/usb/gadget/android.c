/*
 * Gadget Driver for Android
 *
 * Copyright (C) 2008 Google, Inc.
 * Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* #define DEBUG */
/* #define VERBOSE_DEBUG */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/debugfs.h>

#include <linux/usb/android_composite.h>
#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>

#include "gadget_chips.h"

#ifdef CONFIG_LGE_USB_GADGET_DRIVER
#include "u_lgeusb.h"
#endif

/*           
                            
                                   
 */
#define set_device_class(desc, class, subclass, protocol)	\
	do {													\
		desc.bDeviceClass = class;						\
		desc.bDeviceSubClass = subclass;					\
		desc.bDeviceProtocol = protocol;					\
	} while (0)

/*
 * Kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"
#include "composite.c"

MODULE_AUTHOR("Mike Lockwood");
MODULE_DESCRIPTION("Android Composite USB Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

static const char longname[] = "Gadget Android";

/* Default vendor and product IDs, overridden by platform data */
#define VENDOR_ID		0x18D1
#define PRODUCT_ID		0x0001

struct android_dev {
	struct usb_composite_dev *cdev;
	struct usb_configuration *config;
	int num_products;
	struct android_usb_product *products;
	int num_functions;
	char **functions;

	int product_id;
	int version;
};

static struct android_dev *_android_dev;

#ifdef CONFIG_LGE_USB_GADGET_DRIVER
#define MAX_STR_LEN		20
/*                                                    */
#else
#define MAX_STR_LEN		16
/* string IDs are assigned dynamically */
#endif

#define STRING_MANUFACTURER_IDX		0
#define STRING_PRODUCT_IDX		1
#define STRING_SERIAL_IDX		2

char serial_number[MAX_STR_LEN];
/* String Table */
static struct usb_string strings_dev[] = {
	/* These dummy values should be overridden by platform data */
	[STRING_MANUFACTURER_IDX].s = "Android",
	[STRING_PRODUCT_IDX].s = "Android",
	[STRING_SERIAL_IDX].s = "0123456789ABCDEF",
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

static struct usb_device_descriptor device_desc = {
	.bLength              = sizeof(device_desc),
	.bDescriptorType      = USB_DT_DEVICE,
	.bcdUSB               = __constant_cpu_to_le16(0x0200),
	.bDeviceClass         = USB_CLASS_PER_INTERFACE,
	.idVendor             = __constant_cpu_to_le16(VENDOR_ID),
	.idProduct            = __constant_cpu_to_le16(PRODUCT_ID),
	.bcdDevice            = __constant_cpu_to_le16(0xffff),
	.bNumConfigurations   = 1,
};

static struct usb_otg_descriptor otg_descriptor = {
	.bLength =		sizeof otg_descriptor,
	.bDescriptorType =	USB_DT_OTG,
	.bmAttributes =		USB_OTG_SRP | USB_OTG_HNP,
	.bcdOTG               = __constant_cpu_to_le16(0x0200),
};

static const struct usb_descriptor_header *otg_desc[] = {
	(struct usb_descriptor_header *) &otg_descriptor,
	NULL,
};

static struct list_head _functions = LIST_HEAD_INIT(_functions);
static bool _are_functions_bound;

static void android_set_default_product(int pid);

void android_usb_set_connected(int connected)
{
	if (_android_dev && _android_dev->cdev && _android_dev->cdev->gadget) {
		if (connected)
			usb_gadget_connect(_android_dev->cdev->gadget);
		else
			usb_gadget_disconnect(_android_dev->cdev->gadget);
	}
}

static struct android_usb_function *get_function(const char *name)
{
	struct android_usb_function	*f;
	list_for_each_entry(f, &_functions, list) {
		if (!strcmp(name, f->name))
			return f;
	}
	return 0;
}

static bool are_functions_registered(struct android_dev *dev)
{
	char **functions = dev->functions;
	int i;

	/* Look only for functions required by the board config */
	for (i = 0; i < dev->num_functions; i++) {
		char *name = *functions++;
		bool is_match = false;
		/* Could reuse get_function() here, but a reverse search
		 * should yield less comparisons overall */
		struct android_usb_function *f;
		list_for_each_entry_reverse(f, &_functions, list) {
			if (!strcmp(name, f->name)) {
				is_match = true;
				break;
			}
		}
		if (is_match)
			continue;
		else
			return false;
	}

	return true;
}

static bool should_bind_functions(struct android_dev *dev)
{
	/* Don't waste time if the main driver hasn't bound */
	if (!dev->config)
		return false;

	/* Don't waste time if we've already bound the functions */
	if (_are_functions_bound)
		return false;

	/* This call is the most costly, so call it last */
	if (!are_functions_registered(dev))
		return false;

	return true;
}

static void bind_functions(struct android_dev *dev)
{
	struct android_usb_function	*f;
	char **functions = dev->functions;
	int i;

	for (i = 0; i < dev->num_functions; i++) {
		char *name = *functions++;
		f = get_function(name);
		if (f)
			f->bind_config(dev->config);
		else
			pr_err("%s: function %s not found\n", __func__, name);
	}

	_are_functions_bound = true;
	/*
	 * set_alt(), or next config->bind(), sets up
	 * ep->driver_data as needed.
	 */
	usb_ep_autoconfig_reset(dev->cdev->gadget);
}

static int __ref android_bind_config(struct usb_configuration *c)
{
	struct android_dev *dev = _android_dev;

	pr_debug("android_bind_config\n");
	dev->config = c;

	if (should_bind_functions(dev)) {
		bind_functions(dev);
		android_set_default_product(dev->product_id);
	} else {
		/* Defer enumeration until all functions are bounded */
		if (c->cdev && c->cdev->gadget)
			usb_gadget_disconnect(c->cdev->gadget);
	}

	return 0;
}

static int android_setup_config(struct usb_configuration *c,
		const struct usb_ctrlrequest *ctrl);

static struct usb_configuration android_config_driver = {
	.label		= "android",
	.setup		= android_setup_config,
	.bConfigurationValue = 1,
	.bmAttributes	= USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.bMaxPower	= 0xFA, /* 500ma */
};

static int android_setup_config(struct usb_configuration *c,
		const struct usb_ctrlrequest *ctrl)
{
	int i;
	int ret = -EOPNOTSUPP;

	for (i = 0; i < android_config_driver.next_interface_id; i++) {
		if (android_config_driver.interface[i]->setup) {
			ret = android_config_driver.interface[i]->setup(
				android_config_driver.interface[i], ctrl);
			if (ret >= 0)
				return ret;
		}
	}
	return ret;
}

static int product_has_function(struct android_usb_product *p,
		struct usb_function *f)
{
	char **functions = p->functions;
	int count = p->num_functions;
	const char *name = f->name;
	int i;

	for (i = 0; i < count; i++) {
		/* For functions with multiple instances, usb_function.name
		 * will have an index appended to the core name (ex: acm0),
		 * while android_usb_product.functions[i] will only have the
		 * core name (ex: acm). So, only compare up to the length of
		 * android_usb_product.functions[i].
		 */
		if (!strncmp(name, functions[i], strlen(functions[i])))
			return 1;
	}
	return 0;
}

static int product_matches_functions(struct android_usb_product *p)
{
	struct usb_function		*f;
	list_for_each_entry(f, &android_config_driver.functions, list) {
		if (product_has_function(p, f) == !!f->disabled)
			return 0;
	}
	return 1;
}

static int get_product_id(struct android_dev *dev)
{
	struct android_usb_product *p = dev->products;
	int count = dev->num_products;
	int i;

	if (p) {
		for (i = 0; i < count; i++, p++) {
			if (product_matches_functions(p))
				return p->product_id;
		}
	}
	/* use default product ID */
	return dev->product_id;
}

static int is_msc_only_comp(int pid)
{
	struct android_dev *dev = _android_dev;
	struct android_usb_product *up = dev->products;
	int count;
	char **functions;
	int index;

	for (index = 0; index < dev->num_products; index++, up++) {
		if (pid == up->product_id)
			break;
	}

	count = up->num_functions;
	functions = up->functions;

	if (count == 1 && !strncmp(*functions, "usb_mass_storage", 32))
		return true;
	else
		return false;
}

static int __devinit android_bind(struct usb_composite_dev *cdev)
{
	struct android_dev *dev = _android_dev;
	struct usb_gadget	*gadget = cdev->gadget;
	int			gcnum, id, product_id, ret;

	pr_debug("android_bind\n");

	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */
	id = usb_string_id(cdev);
	if (id < 0)
		return id;
	strings_dev[STRING_MANUFACTURER_IDX].id = id;
	device_desc.iManufacturer = id;

	id = usb_string_id(cdev);
	if (id < 0)
		return id;
	strings_dev[STRING_PRODUCT_IDX].id = id;
	device_desc.iProduct = id;

	id = usb_string_id(cdev);
	if (id < 0)
		return id;
	strings_dev[STRING_SERIAL_IDX].id = id;
	device_desc.iSerialNumber = id;

	if (gadget_is_otg(cdev->gadget))
		android_config_driver.descriptors = otg_desc;

	if (!usb_gadget_set_selfpowered(gadget))
		android_config_driver.bmAttributes |= USB_CONFIG_ATT_SELFPOWER;
	/*
	 * Supporting remote wakeup for mass storage only function
	 * doesn't make sense, since there is no notifications that
	 * that can be sent from mass storage during suspend.
	 */
	if (gadget->ops->wakeup && !is_msc_only_comp((dev->product_id)))
		android_config_driver.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	else
		android_config_driver.bmAttributes &= ~USB_CONFIG_ATT_WAKEUP;


	/* register our configuration */
	ret = usb_add_config(cdev, &android_config_driver, android_bind_config);
	if (ret) {
		pr_err("%s: usb_add_config failed\n", __func__);
		return ret;
	}

	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0)
		device_desc.bcdDevice = cpu_to_le16(0x0200 + gcnum);
	else {
		/* gadget zero is so simple (for now, no altsettings) that
		 * it SHOULD NOT have problems with bulk-capable hardware.
		 * so just warn about unrcognized controllers -- don't panic.
		 *
		 * things like configuration and altsetting numbering
		 * can need hardware-specific attention though.
		 */
		pr_warning("%s: controller '%s' not recognized\n",
			longname, gadget->name);
		device_desc.bcdDevice = __constant_cpu_to_le16(0x9999);
	}

	usb_gadget_set_selfpowered(gadget);
	dev->cdev = cdev;
	product_id = get_product_id(dev);
	device_desc.idProduct = __constant_cpu_to_le16(product_id);
	cdev->desc.idProduct = device_desc.idProduct;

#ifdef CONFIG_LGE_USB_GADGET_DRIVER
	/* Set default device class */
 	if (product_id == LGE_FACTORY_PID)
		set_device_class(device_desc, USB_CLASS_COMM, 0x00, 0x00);
	else
		set_device_class(device_desc, USB_CLASS_MISC, 0x02, 0x01);
#endif /*                              */

	return 0;
}

static struct usb_composite_driver android_usb_driver = {
	.name		= "android_usb",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.enable_function = android_enable_function,
};

static bool is_func_supported(struct android_usb_function *f)
{
	char **functions = _android_dev->functions;
	int count = _android_dev->num_functions;
	const char *name = f->name;
	int i;

	for (i = 0; i < count; i++) {
		if (!strcmp(*functions++, name))
			return true;
	}
	return false;
}

void android_register_function(struct android_usb_function *f)
{
	struct android_dev *dev = _android_dev;

	pr_debug("%s: %s\n", __func__, f->name);

	if (!is_func_supported(f))
		return;

	list_add_tail(&f->list, &_functions);

	if (dev && should_bind_functions(dev)) {
		bind_functions(dev);
		android_set_default_product(dev->product_id);
		/* All function are bounded. Enable enumeration */
		if (dev->cdev && dev->cdev->gadget)
			usb_gadget_connect(dev->cdev->gadget);
	}

}

/**
 * android_set_function_mask() - enables functions based on selected pid.
 * @up: selected product id pointer
 *
 * This function enables functions related with selected product id.
 */
static void android_set_function_mask(struct android_usb_product *up)
{
	int index, found = 0;
	struct usb_function *func;

	list_for_each_entry(func, &android_config_driver.functions, list) {
		/* adb function enable/disable handled separetely */
		if (!strcmp(func->name, "adb") && !func->disabled)
			continue;

		for (index = 0; index < up->num_functions; index++) {
			if (!strcmp(up->functions[index], func->name)) {
				found = 1;
				break;
			}
		}

		if (found) { /* func is part of product. */
			/* if func is disabled, enable the same. */
			if (func->disabled)
				usb_function_set_enabled(func, 1);
			found = 0;
		} else { /* func is not part if product. */
			/* if func is enabled, disable the same. */
			if (!func->disabled)
				usb_function_set_enabled(func, 0);
		}
	}
}

/**
 * android_set_defaut_product() - selects default product id and enables
 * required functions
 * @product_id: default product id
 *
 * This function selects default product id using pdata information and
 * enables functions for same.
*/
static void android_set_default_product(int pid)
{
	struct android_dev *dev = _android_dev;
	struct android_usb_product *up = dev->products;
	int index;

	for (index = 0; index < dev->num_products; index++, up++) {
		if (pid == up->product_id)
			break;
	}
	android_set_function_mask(up);
	device_desc.idProduct = __constant_cpu_to_le16(pid);
	if (dev->cdev)
		dev->cdev->desc.idProduct = device_desc.idProduct;
}

/**
 * android_config_functions() - selects product id based on function need
 * to be enabled / disabled.
 * @f: usb function
 * @enable : function needs to be enable or disable
 *
 * This function selects first product id having required function.
 * RNDIS/MTP function enable/disable uses this.
*/
#ifdef CONFIG_USB_ANDROID_RNDIS
static void android_config_functions(struct usb_function *f, int enable)
{
	struct android_dev *dev = _android_dev;
	struct android_usb_product *up = dev->products;
	int index;

	/* Searches for product id having function */
	if (enable) {
		for (index = 0; index < dev->num_products; index++, up++) {
			if (product_has_function(up, f))
				break;
		}
		android_set_function_mask(up);
	} else
		android_set_default_product(dev->product_id);
}
#endif

#ifdef CONFIG_LGE_USB_GADGET_DRIVER
static void android_set_class_product(int pid, int class_code)
{
	struct android_dev *dev = _android_dev;
	int subclass, protocol;

	switch(class_code) {
		case USB_CLASS_PER_INTERFACE:
			subclass = 0x00;
			protocol = 0x00;
			break;
		case USB_CLASS_COMM:
			subclass = 0x00;
			protocol = 0x00;
			break;
		case USB_CLASS_MISC:
			subclass = 0x02;
			protocol = 0x01;
			break;
		default:
			subclass = 0xFF;
			protocol = 0xFF;
			break;
	}
	set_device_class(dev->cdev->desc, class_code, subclass, protocol);
	android_set_default_product(pid);
}

static void android_force_reset(void)
{
	struct android_dev *dev = _android_dev;
	int product_id;

	product_id = get_product_id(dev);
	device_desc.idProduct = __constant_cpu_to_le16(product_id);
	if (dev->cdev)
		dev->cdev->desc.idProduct = device_desc.idProduct;

	usb_composite_force_reset(dev->cdev);
}

static void android_force_sw_reset(void)
{
	struct android_dev *dev = _android_dev;
	int product_id;

	product_id = get_product_id(dev);
	device_desc.idProduct = __constant_cpu_to_le16(product_id);
	if (dev->cdev)
		dev->cdev->desc.idProduct = device_desc.idProduct;

	usb_composite_force_sw_reset(dev->cdev);
}

/* usb mode switching by product id */
static int android_set_pid(const char *val, struct kernel_param *kp)
{
	struct android_dev *dev = _android_dev;
	int ret = 0;
	unsigned long pid;

	// skip pid change request for factory cable
	if (lge_is_factory_cable(NULL, NULL))
		return -1;

	if (strict_strtol(val, 16, &pid))
		return -1;
	else
		pid &= 0x0000ffff;

	if (dev == NULL) {
		device_desc.idProduct = (u16) pid;
		return -1;
	}

	// skip same pid switching
	if (pid == 0x6000 || android_lge_get_current_pid() == pid)
		return -1;

	// finally switch pid and reset driver
	android_lge_switch_driver(pid, 1);
	return ret;
}

static int android_get_pid(char *buffer, struct kernel_param *kp)
{
	return sprintf(buffer, "%x", android_lge_get_current_pid());
}
module_param_call(product_id, android_set_pid, android_get_pid, NULL, 0664);
MODULE_PARM_DESC(product_id, "USB device product id");
#endif

void update_dev_desc(struct android_dev *dev)
{
	struct usb_function *f;
	struct usb_function *last_enabled_f = NULL;
	int num_enabled = 0;
	int has_iad = 0;

	dev->cdev->desc.bDeviceClass = USB_CLASS_PER_INTERFACE;
	dev->cdev->desc.bDeviceSubClass = 0x00;
	dev->cdev->desc.bDeviceProtocol = 0x00;

	list_for_each_entry(f, &android_config_driver.functions, list) {
		if (!f->disabled) {
			num_enabled++;
			last_enabled_f = f;
			if (f->descriptors[0]->bDescriptorType ==
					USB_DT_INTERFACE_ASSOCIATION)
				has_iad = 1;
		}
		if (num_enabled > 1 && has_iad) {
			dev->cdev->desc.bDeviceClass = USB_CLASS_MISC;
			dev->cdev->desc.bDeviceSubClass = 0x02;
			dev->cdev->desc.bDeviceProtocol = 0x01;
			break;
		}
	}

	if (num_enabled == 1) {
#ifdef CONFIG_USB_ANDROID_RNDIS
		if (!strcmp(last_enabled_f->name, "rndis")) {
#ifdef CONFIG_USB_ANDROID_RNDIS_WCEIS
			dev->cdev->desc.bDeviceClass =
					USB_CLASS_WIRELESS_CONTROLLER;
#else
			dev->cdev->desc.bDeviceClass = USB_CLASS_COMM;
#endif
		}
#endif
	}
}


static char *sysfs_allowed[] = {
#ifdef CONFIG_LGE_USB_GADGET_DRIVER
	"usb_mass_storage",
	"acm",
#endif
	"rndis",
	"mtp",
	"adb",
};

static int is_sysfschange_allowed(struct usb_function *f)
{
	char **functions = sysfs_allowed;
	int count = ARRAY_SIZE(sysfs_allowed);
	int i;

	for (i = 0; i < count; i++) {
		if (!strncmp(f->name, functions[i], 32))
			return 1;
	}
	return 0;
}

int android_enable_function(struct usb_function *f, int enable)
{
	struct android_dev *dev = _android_dev;
	int disable = !enable;
	struct usb_gadget	*gadget = dev->cdev->gadget;
	int product_id;

	pr_info_ratelimited("%s: %s %s\n",
		__func__, enable ? "enable" : "disable", f->name);

	if (!is_sysfschange_allowed(f))
		return -EINVAL;

#ifdef CONFIG_LGE_USB_GADGET_DRIVER
	if (!strcmp(f->name, "usb_mass_storage")) {
		/* We force to change mode even if mass storage is already enabled */
		f->disabled = disable;
		if (enable) {
			/* switch to mass storage only */
			android_set_class_product(LGE_UMSONLY_PID, USB_CLASS_PER_INTERFACE);
			android_force_reset();
			return 0;
			
		} else {
			android_set_class_product(dev->product_id, USB_CLASS_MISC);
			android_force_sw_reset();
			return 0;
		}
	}

	if (!strcmp(f->name, "acm")) {
		/* We force to change mode even if already enabled */
		f->disabled = disable;
		if (enable) {
			/* switch to modem(default) mode */
			android_set_class_product(LGE_DEFAULT_PID, USB_CLASS_MISC);
		} else {
			android_set_class_product(dev->product_id, USB_CLASS_MISC);
		}

		android_force_reset();
		return 0;
	}
#endif

	if (!!f->disabled != disable) {
		usb_function_set_enabled(f, !disable);

#ifdef CONFIG_USB_ANDROID_RNDIS
		if (!strcmp(f->name, "rndis")) {

			/* We need to specify the COMM class in the device descriptor
			 * if we are using RNDIS.
			 */
			if (enable) {
#ifdef CONFIG_USB_ANDROID_RNDIS_WCEIS
				dev->cdev->desc.bDeviceClass = USB_CLASS_MISC;
				dev->cdev->desc.bDeviceSubClass      = 0x02;
				dev->cdev->desc.bDeviceProtocol      = 0x01;
#else
				dev->cdev->desc.bDeviceClass = USB_CLASS_COMM;
#endif
			} else {
				dev->cdev->desc.bDeviceClass = USB_CLASS_PER_INTERFACE;
				dev->cdev->desc.bDeviceSubClass      = 0;
				dev->cdev->desc.bDeviceProtocol      = 0;
			}

			android_config_functions(f, enable);
		}
#endif

#ifdef CONFIG_USB_ANDROID_MTP
		if (!strcmp(f->name, "mtp"))
			android_config_functions(f, enable);
#endif

#ifdef CONFIG_LGE_USB_GADGET_DRIVER
		if (device_desc.idProduct == LGE_UMSONLY_PID ||
			device_desc.idProduct == LGE_DEFAULT_PID) {

			/* When adb enable during mass storage only */
			if (!strcmp(f->name, "adb")) {
				set_device_class(dev->cdev->desc, USB_CLASS_MISC, 0x02, 0x01);
				android_set_default_product(dev->product_id);
			}
		}
#endif
		product_id = get_product_id(dev);

		if (gadget && gadget->ops->wakeup &&
				!is_msc_only_comp((product_id)))
			android_config_driver.bmAttributes |=
				USB_CONFIG_ATT_WAKEUP;
		else
			android_config_driver.bmAttributes &=
				~USB_CONFIG_ATT_WAKEUP;

		device_desc.idProduct = __constant_cpu_to_le16(product_id);
		if (dev->cdev)
			dev->cdev->desc.idProduct = device_desc.idProduct;
		usb_composite_force_reset(dev->cdev);
	}
	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int android_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t android_debugfs_serialno_write(struct file *file, const char
				__user *buf,	size_t count, loff_t *ppos)
{
	char str_buf[MAX_STR_LEN];

	if (count > MAX_STR_LEN)
		return -EFAULT;

	if (copy_from_user(str_buf, buf, count))
		return -EFAULT;

	memcpy(serial_number, str_buf, count);

	if (serial_number[count - 1] == '\n')
		serial_number[count - 1] = '\0';

	strings_dev[STRING_SERIAL_IDX].s = serial_number;

	return count;
}
const struct file_operations android_fops = {
	.open	= android_debugfs_open,
	.write	= android_debugfs_serialno_write,
};

struct dentry *android_debug_root;
struct dentry *android_debug_serialno;

static int android_debugfs_init(struct android_dev *dev)
{
	android_debug_root = debugfs_create_dir("android", NULL);
	if (!android_debug_root)
		return -ENOENT;

	android_debug_serialno = debugfs_create_file("serial_number", 0222,
						android_debug_root, dev,
						&android_fops);
	if (!android_debug_serialno) {
		debugfs_remove(android_debug_root);
		android_debug_root = NULL;
		return -ENOENT;
	}
	return 0;
}

static void android_debugfs_cleanup(void)
{
       debugfs_remove(android_debug_serialno);
       debugfs_remove(android_debug_root);
}
#endif

#ifdef CONFIG_LGE_USB_GADGET_DRIVER
void android_lge_switch_driver(int pid, int need_reset)
{
	struct android_dev *dev = _android_dev;

	android_set_default_product(pid);

	if (pid == LGE_FACTORY_PID) {
		device_desc.iSerialNumber = 0;
		if (dev->cdev)
			dev->cdev->desc.iSerialNumber = 0;
	}

	device_desc.idProduct = __constant_cpu_to_le16(pid);
	if (dev->cdev)
		dev->cdev->desc.idProduct = device_desc.idProduct;

	if (need_reset)
		usb_composite_force_reset(dev->cdev);
}
EXPORT_SYMBOL(android_lge_switch_driver);

int android_lge_get_current_pid(void)
{
	struct android_dev *dev = _android_dev;

	if (dev == NULL)
		return 0;

	return get_product_id(dev);
}
EXPORT_SYMBOL(android_lge_get_current_pid);
#endif

static int __devinit android_probe(struct platform_device *pdev)
{
	struct android_usb_platform_data *pdata = pdev->dev.platform_data;
	struct android_dev *dev = _android_dev;
	int result;

	dev_dbg(&pdev->dev, "%s: pdata: %p\n", __func__, pdata);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	result = pm_runtime_get(&pdev->dev);
	if (result < 0) {
		dev_err(&pdev->dev,
			"Runtime PM: Unable to wake up the device, rc = %d\n",
			result);
		return result;
	}

	if (pdata) {
		dev->products = pdata->products;
		dev->num_products = pdata->num_products;
		dev->functions = pdata->functions;
		dev->num_functions = pdata->num_functions;
		if (pdata->vendor_id)
			device_desc.idVendor =
				__constant_cpu_to_le16(pdata->vendor_id);
		if (pdata->product_id) {
			dev->product_id = pdata->product_id;
			device_desc.idProduct =
				__constant_cpu_to_le16(pdata->product_id);
		}
		if (pdata->version)
			dev->version = pdata->version;

		if (pdata->product_name)
			strings_dev[STRING_PRODUCT_IDX].s = pdata->product_name;
		if (pdata->manufacturer_name)
			strings_dev[STRING_MANUFACTURER_IDX].s =
					pdata->manufacturer_name;
		if (pdata->serial_number)
			strings_dev[STRING_SERIAL_IDX].s = pdata->serial_number;
	}
#ifdef CONFIG_DEBUG_FS
	result = android_debugfs_init(dev);
	if (result)
		pr_debug("%s: android_debugfs_init failed\n", __func__);
#endif
	return usb_composite_probe(&android_usb_driver, android_bind);
}

static int andr_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "pm_runtime: suspending...\n");
	return 0;
}

static int andr_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "pm_runtime: resuming...\n");
	return 0;
}

static struct dev_pm_ops andr_dev_pm_ops = {
	.runtime_suspend = andr_runtime_suspend,
	.runtime_resume = andr_runtime_resume,
};

static struct platform_driver android_platform_driver = {
	.driver = { .name = "android_usb", .pm = &andr_dev_pm_ops},
};

static int __init init(void)
{
	struct android_dev *dev;

	pr_debug("android init\n");

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	/* set default values, which should be overridden by platform data */
	dev->product_id = PRODUCT_ID;
	_android_dev = dev;

	return platform_driver_probe(&android_platform_driver, android_probe);
}
module_init(init);

static void __exit cleanup(void)
{
#ifdef CONFIG_DEBUG_FS
	android_debugfs_cleanup();
#endif
	usb_composite_unregister(&android_usb_driver);
	platform_driver_unregister(&android_platform_driver);
	kfree(_android_dev);
	_android_dev = NULL;
}
module_exit(cleanup);
