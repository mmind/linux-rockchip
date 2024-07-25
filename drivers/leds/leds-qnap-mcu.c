// SPDX-License-Identifier: GPL-2.0-only

/*
 * Driver for LEDs found on QNAP MCU devices
 *
 * Copyright (C) 2024 Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/leds.h>
#include <linux/mfd/qnap-mcu.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <uapi/linux/uleds.h>

enum qnap_mcu_err_led_mode {
	QNAP_MCU_ERR_LED_ON = 0,
	QNAP_MCU_ERR_LED_OFF = 1,
	QNAP_MCU_ERR_LED_BLINK_FAST = 2,
	QNAP_MCU_ERR_LED_BLINK_SLOW = 3,
};

struct qnap_mcu_err_led {
	struct qnap_mcu *mcu;
	struct led_classdev cdev;
	char name[LED_MAX_NAME_SIZE];
	u8 num;
	u8 mode;
};

static inline struct qnap_mcu_err_led *
		cdev_to_qnap_mcu_err_led(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct qnap_mcu_err_led, cdev);
}

static int qnap_mcu_err_led_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	struct qnap_mcu_err_led *err_led = cdev_to_qnap_mcu_err_led(led_cdev);
	u8 cmd[] = {
		[0] = 0x40,
		[1] = 0x52,
		[2] = 0x30 + err_led->num,
		[3] = 0x30
	};

	/*
	 * If the led is off, turn it on. Otherwise don't disturb
	 * a possible set blink-mode.
	 */
	if (value == 0)
		err_led->mode = QNAP_MCU_ERR_LED_OFF;
	else if (err_led->mode == QNAP_MCU_ERR_LED_OFF)
		err_led->mode = QNAP_MCU_ERR_LED_ON;

	cmd[3] = 0x30 + err_led->mode;

	return qnap_mcu_exec_with_ack(err_led->mcu, cmd, sizeof(cmd));
}

static int qnap_mcu_err_led_blink_set(struct led_classdev *led_cdev,
				      unsigned long *delay_on,
				      unsigned long *delay_off)
{
	struct qnap_mcu_err_led *err_led = cdev_to_qnap_mcu_err_led(led_cdev);
	u8 cmd[] = {
		[0] = 0x40,
		[1] = 0x52,
		[2] = 0x30 + err_led->num,
		[3] = 0x30
	};

	/* LED is off, nothing to do */
	if (err_led->mode == QNAP_MCU_ERR_LED_OFF)
		return 0;

	if (*delay_on < 500) {
		*delay_on = 100;
		*delay_off = 100;
		err_led->mode = QNAP_MCU_ERR_LED_BLINK_FAST;
	} else {
		*delay_on = 500;
		*delay_off = 500;
		err_led->mode = QNAP_MCU_ERR_LED_BLINK_SLOW;
	}

	cmd[3] = 0x30 + err_led->mode;

	return qnap_mcu_exec_with_ack(err_led->mcu, cmd, sizeof(cmd));
}

static int qnap_mcu_register_err_led(struct device *dev, struct qnap_mcu *mcu, int num)
{
	struct qnap_mcu_err_led *err_led;
	int ret;

	err_led = devm_kzalloc(dev, sizeof(*err_led), GFP_KERNEL);
	if (!err_led)
		return -ENOMEM;

	err_led->mcu = mcu;
	err_led->num = num;
	err_led->mode = QNAP_MCU_ERR_LED_OFF;

	snprintf(err_led->name, LED_MAX_NAME_SIZE, "hdd%d:red:status", num + 1);
	err_led->cdev.name = err_led->name;

	err_led->cdev.brightness_set_blocking = qnap_mcu_err_led_set;
	err_led->cdev.blink_set = qnap_mcu_err_led_blink_set;
	err_led->cdev.brightness = 0;
	err_led->cdev.max_brightness = 1;

	ret = devm_led_classdev_register(dev, &err_led->cdev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register hdd led %d", num);

	return qnap_mcu_err_led_set(&err_led->cdev, 0);
}

enum qnap_mcu_usb_led_mode {
	QNAP_MCU_USB_LED_ON = 1,
	QNAP_MCU_USB_LED_OFF = 3,
	QNAP_MCU_USB_LED_BLINK = 2,
};

struct qnap_mcu_usb_led {
	struct qnap_mcu *mcu;
	struct led_classdev cdev;
	u8 mode;
};

static inline struct qnap_mcu_usb_led *
		cdev_to_qnap_mcu_usb_led(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct qnap_mcu_usb_led, cdev);
}

static int qnap_mcu_usb_led_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	struct qnap_mcu_usb_led *usb_led = cdev_to_qnap_mcu_usb_led(led_cdev);
	u8 cmd[] = {
		[0] = 0x40,
		[1] = 0x43,
		[2] = 0
	};

	/*
	 * If the led is off, turn it on. Otherwise don't disturb
	 * a possible set blink-mode.
	 */
	if (value == 0)
		usb_led->mode = QNAP_MCU_USB_LED_OFF;
	else if (usb_led->mode == QNAP_MCU_USB_LED_OFF)
		usb_led->mode = QNAP_MCU_USB_LED_ON;

	/* byte 3 is shared between the usb led target and setting the mode */
	cmd[2] = 0x44 | usb_led->mode;

	return qnap_mcu_exec_with_ack(usb_led->mcu, cmd, sizeof(cmd));
}

static int qnap_mcu_usb_led_blink_set(struct led_classdev *led_cdev,
				      unsigned long *delay_on,
				      unsigned long *delay_off)
{
	struct qnap_mcu_usb_led *usb_led = cdev_to_qnap_mcu_usb_led(led_cdev);
	u8 cmd[] = {
		[0] = 0x40,
		[1] = 0x43,
		[2] = 0
	};

	/* LED is off, nothing to do */
	if (usb_led->mode == QNAP_MCU_USB_LED_OFF)
		return 0;

	*delay_on = 250;
	*delay_off = 250;
	usb_led->mode = QNAP_MCU_USB_LED_BLINK;

	/* byte 3 is shared between the usb led target and setting the mode */
	cmd[2] = 0x44 | usb_led->mode;

	return qnap_mcu_exec_with_ack(usb_led->mcu, cmd, sizeof(cmd));
}

static int qnap_mcu_register_usb_led(struct device *dev, struct qnap_mcu *mcu)
{
	struct qnap_mcu_usb_led *usb_led;
	int ret;

	usb_led = devm_kzalloc(dev, sizeof(*usb_led), GFP_KERNEL);
	if (!usb_led)
		return -ENOMEM;

	usb_led->mcu = mcu;
	usb_led->mode = QNAP_MCU_USB_LED_OFF;
	usb_led->cdev.name = "usb:blue:disk";
	usb_led->cdev.brightness_set_blocking = qnap_mcu_usb_led_set;
	usb_led->cdev.blink_set = qnap_mcu_usb_led_blink_set;
	usb_led->cdev.brightness = 0;
	usb_led->cdev.max_brightness = 1;

	ret = devm_led_classdev_register(dev, &usb_led->cdev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register usb led");

	return qnap_mcu_usb_led_set(&usb_led->cdev, 0);
}

static int qnap_mcu_leds_probe(struct platform_device *pdev)
{
	struct qnap_mcu *mcu = dev_get_drvdata(pdev->dev.parent);
	const struct qnap_mcu_variant *variant = qnap_mcu_get_variant_data(mcu);
	int ret, i;

	for (i = 0; i < variant->num_drives; i++) {
		ret = qnap_mcu_register_err_led(&pdev->dev, mcu, i);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					"failed to register error led %d\n", i);
	}

	if (variant->usb_led) {
		ret = qnap_mcu_register_usb_led(&pdev->dev, mcu);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					"failed to register usb led %d\n", i);
	}

	return 0;
}

static struct platform_driver qnap_mcu_leds_driver = {
	.probe = qnap_mcu_leds_probe,
	.driver = {
		.name = "qnap-mcu-leds",
	},
};
module_platform_driver(qnap_mcu_leds_driver);

MODULE_ALIAS("platform:qnap-mcu-leds");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("QNAP MCU LEDs driver");
MODULE_LICENSE("GPL");
