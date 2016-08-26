/*
 * Google Servo USB driver
 *
 * Copyright (C) 2003 David Glance <davidgsf@sourceforge.net>
 *               2001-2004 Juergen Stuber <starblue@users.sourceforge.net>
 *               2015 Collabora Ltd. <daniels@collabora.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 *
 * derived from USB Lego Tower driver
 * derived from USB Skeleton driver - 0.5
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <linux/usb.h>

#define GOOGLE_SERVO_MINOR_BASE	160

#define GOOGLE_USB_VENDOR_ID	0x18d1

#define SERVO_V2_PRODUCT_ID	0x5002
#define SERVO_V3_PRODUCT_ID	0x5004
#define SERVO_MICRO_PRODUCT_ID	0x501a
#define SERVO_V4_PRODUCT_ID	0x501b

static const struct usb_device_id servo_table[] = {
	{ USB_DEVICE(GOOGLE_USB_VENDOR_ID, SERVO_V2_PRODUCT_ID) },
	{ USB_DEVICE(GOOGLE_USB_VENDOR_ID, SERVO_V3_PRODUCT_ID) },
	{ USB_DEVICE(GOOGLE_USB_VENDOR_ID, SERVO_MICRO_PRODUCT_ID) },
	{ USB_DEVICE(GOOGLE_USB_VENDOR_ID, SERVO_V4_PRODUCT_ID) },
	{ }
};
MODULE_DEVICE_TABLE (usb, servo_table);

static const struct file_operations servo_fops = {
	.owner =	THIS_MODULE,
	.read  =	NULL,
	.write =	NULL,
	.open =		NULL,
	.release =	NULL,
	.poll =		NULL,
	.llseek =	NULL,
};

static char *servo_devnode(const struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "usb/%s", dev_name(dev));
}

static struct usb_class_driver servo_class = {
	.name =		"google-servo%d",
	.devnode =	servo_devnode,
	.fops =		&servo_fops,
	.minor_base =	GOOGLE_SERVO_MINOR_BASE,
};

static DEFINE_MUTEX(open_disc_mutex);

/**
 * Register the Servo device with the USB core
 */
static int servo_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct device *idev = &interface->dev;
	struct usb_device *udev = interface_to_usbdev(interface);
	int retval = 0;

	if (interface->cur_altsetting->desc.bInterfaceNumber != 0) {
		printk("servo: ignoring interface %d\n", interface->cur_altsetting->desc.bInterfaceNumber);
		return -ENODEV;
	}

	mutex_lock(&open_disc_mutex);

	usb_set_intfdata(interface, udev);
	retval = usb_register_dev(interface, &servo_class);
	if (retval) {
		/* something prevented us from registering this driver */
		dev_err(idev, "Not able to get a minor for this device.\n");
		goto error;
	}

	mutex_unlock(&open_disc_mutex);

	return retval;

error:
	usb_set_intfdata(interface, NULL);
	mutex_unlock(&open_disc_mutex);
	return retval;
}

/**
 * Unregister a device from the USB core
 */
static void servo_disconnect(struct usb_interface *interface)
{
	mutex_lock(&open_disc_mutex);
	usb_set_intfdata(interface, NULL);
	usb_deregister_dev(interface, &servo_class);
	mutex_unlock(&open_disc_mutex);
}

static struct usb_driver servo_driver = {
	.name =		"google-servo",
	.probe =	servo_probe,
	.disconnect =	servo_disconnect,
	.id_table =	servo_table,
};

module_usb_driver(servo_driver);

/* Version Information */
#define DRIVER_VERSION "v0.1"
#define DRIVER_AUTHOR "Daniel Stone <daniels@collabora.com>"
#define DRIVER_DESC "Google Servo USB Driver"

MODULE_AUTHOR("Daniel Stone <daniels@collabora.com>");
MODULE_DESCRIPTION("Google Servo USB control board");
MODULE_LICENSE("GPL");
