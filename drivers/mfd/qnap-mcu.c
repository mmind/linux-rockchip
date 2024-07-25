// SPDX-License-Identifier: GPL-2.0-only

/*
 * MFD core driver for the MCU in Qnap NAS devices that is connected
 * via a dedicated UART port
 *
 * Copyright (C) 2024 Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/mfd/core.h>
#include <linux/mfd/qnap-mcu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/serdev.h>

/* The longest command found so far is 5 bytes long */
#define QNAP_MCU_MAX_CMD_SIZE		5
#define QNAP_MCU_MAX_DATA_SIZE		36
#define QNAP_MCU_CHECKSUM_SIZE		1

#define QNAP_MCU_RX_BUFFER_SIZE		\
		(QNAP_MCU_MAX_DATA_SIZE + QNAP_MCU_CHECKSUM_SIZE)

#define QNAP_MCU_TX_BUFFER_SIZE		\
		(QNAP_MCU_MAX_CMD_SIZE + QNAP_MCU_CHECKSUM_SIZE)

/**
 * struct qnap_mcu_reply - Reply to a command
 *
 * @data:	Buffer to store reply payload in
 * @length:	Expected reply length, including the checksum
 * @received:	So far received number of bytes
 * @done:	Reply received completely
 */
struct qnap_mcu_reply {
	u8 *data;
	size_t length;
	size_t received;
	struct completion done;
};

/**
 * struct qnap_mcu - QNAP NAS embedded controller
 *
 * @serdev:	Pointer to underlying serdev
 * @bus_lock:	Lock to serialize access to the device
 * @reply_lock:	Lock protecting @reply
 * @reply:	Pointer to memory to store reply payload
 * @variant:	Device variant specific information
 * @version:	MCU firmware version
 */
struct qnap_mcu {
	struct serdev_device *serdev;
	/* Serialize access to the device */
	struct mutex bus_lock;
	/* Protect access to the reply pointer */
	struct mutex reply_lock;
	struct qnap_mcu_reply *reply;
	const struct qnap_mcu_variant *variant;
	u8 version[4];
};

/*
 * The QNAP-MCU uses a basic XOR checksum.
 * It is always the last byte and XORs the whole previous message.
 */
static u8 qnap_mcu_csum(const u8 *buf, size_t size)
{
	u8 csum = 0;

	while (size--)
		csum ^= *buf++;

	return csum;
}

static int qnap_mcu_write(struct qnap_mcu *sp, const u8 *data, u8 data_size)
{
	unsigned char tx[QNAP_MCU_TX_BUFFER_SIZE];
	size_t length = data_size + QNAP_MCU_CHECKSUM_SIZE;

	if (WARN_ON(length > sizeof(tx)))
		return -ENOMEM;

	memcpy(tx, data, data_size);
	tx[data_size] = qnap_mcu_csum(data, data_size);

	print_hex_dump_debug("qnap-mcu tx: ", DUMP_PREFIX_NONE,
			     16, 1, tx, length, false);

	return serdev_device_write(sp->serdev, tx, length, HZ);
}

static size_t qnap_mcu_receive_buf(struct serdev_device *serdev,
				   const u8 *buf, size_t size)
{
	struct device *dev = &serdev->dev;
	struct qnap_mcu *mcu = dev_get_drvdata(dev);
	struct qnap_mcu_reply *reply = mcu->reply;
	const u8 *src = buf;
	const u8 *end = buf + size;

	mutex_lock(&mcu->reply_lock);
	if (!reply) {
		dev_warn(dev, "received %zu bytes, we were not waiting for\n",
			 size);
		mutex_unlock(&mcu->reply_lock);
		return size;
	}

	while (src < end) {
		reply->data[reply->received] = *src++;
		reply->received++;

		if (reply->received == reply->length) {
			complete(&reply->done);
			mutex_unlock(&mcu->reply_lock);

			/*
			 * We report the consumed number of bytes. If there
			 * are still bytes remaining (though there shouldn't)
			 * the serdev layer will re-execute this handler with
			 * the remainder of the Rx bytes.
			 */
			return src - buf;
		}
	}

	/*
	 * The only way to get out of the above loop and end up here
	 * is through consuming all of the supplied data, so here we
	 * report that we processed it all.
	 */
	mutex_unlock(&mcu->reply_lock);
	return size;
}

static const struct serdev_device_ops qnap_mcu_serdev_device_ops = {
	.receive_buf  = qnap_mcu_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

int qnap_mcu_exec(struct qnap_mcu *mcu,
		  const u8 *cmd_data, size_t cmd_data_size,
		  u8 *reply_data, size_t reply_data_size)
{
	unsigned char rx[QNAP_MCU_RX_BUFFER_SIZE];
	size_t length = reply_data_size + QNAP_MCU_CHECKSUM_SIZE;
	struct qnap_mcu_reply reply = {
		.data     = rx,
		.length   = length,
		.received = 0,
		.done     = COMPLETION_INITIALIZER_ONSTACK(reply.done),
	};
	int ret;

	if (WARN_ON(length > sizeof(rx)))
		return -ENOMEM;

	mutex_lock(&mcu->bus_lock);

	mutex_lock(&mcu->reply_lock);
	mcu->reply = &reply;
	mutex_unlock(&mcu->reply_lock);

	qnap_mcu_write(mcu, cmd_data, cmd_data_size);

	if (!wait_for_completion_timeout(&reply.done,
					 msecs_to_jiffies(500))) {
		dev_err(&mcu->serdev->dev, "Command timeout\n");
		ret = -ETIMEDOUT;
	} else {
		u8 crc = qnap_mcu_csum(rx, reply_data_size);

		print_hex_dump_debug("qnap-mcu rx: ", DUMP_PREFIX_NONE,
				     16, 1, rx, length, false);

		if (crc != rx[reply_data_size]) {
			dev_err(&mcu->serdev->dev,
				"Checksum 0x%02x wrong for data\n", crc);
			ret = -EIO;
		} else {
			memcpy(reply_data, rx, reply_data_size);
			ret = 0;
		}
	}

	mutex_lock(&mcu->reply_lock);
	mcu->reply = NULL;
	mutex_unlock(&mcu->reply_lock);

	mutex_unlock(&mcu->bus_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(qnap_mcu_exec);

int qnap_mcu_exec_with_ack(struct qnap_mcu *mcu,
			   const u8 *cmd_data, size_t cmd_data_size)
{
	u8 ack[2];
	int ret;

	ret = qnap_mcu_exec(mcu, cmd_data, cmd_data_size, ack, sizeof(ack));
	if (ret)
		return ret;

	/* Should return @0 */
	if (ack[0] != 0x40 || ack[1] != 0x30) {
		dev_err(&mcu->serdev->dev, "Did not receive ack\n");
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qnap_mcu_exec_with_ack);

const struct qnap_mcu_variant *qnap_mcu_get_variant_data(struct qnap_mcu *mcu)
{
	return mcu->variant;
}
EXPORT_SYMBOL_GPL(qnap_mcu_get_variant_data);

static int qnap_mcu_get_version(struct qnap_mcu *mcu)
{
	u8 cmd[] = {
		[0] = 0x25, /* % */
		[1] = 0x56  /* V */
	};
	u8 rx[14];
	int ret;

	ret = qnap_mcu_exec(mcu, cmd, sizeof(cmd), rx, 6);
	if (ret)
		return ret;

	memcpy(mcu->version, &rx[2], 4);

	return 0;
}

/*
 * The MCU controls power to the peripherals but not the CPU.
 *
 * So using the pmic to power off the system keeps the MCU and hard-drives
 * running. This also then prevents the system from turning back on until
 * the MCU is turned off by unplugging the power-cable.
 * Turning off the MCU alone on the other hand turns off the hard-drives,
 * LEDs, etc while the main SoC stays running - including its network ports.
 */
static int qnap_mcu_power_off(struct sys_off_data *data)
{
	struct qnap_mcu *mcu = data->cb_data;
	int ret;
	u8 cmd[] = {
		[0] = 0x40, /* @ */
		[1] = 0x43, /* C */
		[2] = 0x30  /* 0 */
	};

	dev_dbg(&mcu->serdev->dev, "running MCU poweroff\n");
	ret = qnap_mcu_exec_with_ack(mcu, cmd, sizeof(cmd));
	if (ret) {
		dev_err(&mcu->serdev->dev, "MCU poweroff failed %d\n", ret);
		return NOTIFY_STOP;
	}

	return NOTIFY_DONE;
}

static const struct qnap_mcu_variant qnap_ts433_mcu = {
	.baud_rate = 115200,
	.num_drives = 4,
	.fan_pwm_min = 51,  /* specified in original model.conf */
	.fan_pwm_max = 255,
	.usb_led = true,
};

static const struct of_device_id qnap_mcu_dt_ids[] = {
	{ .compatible = "qnap,ts433-mcu", .data = &qnap_ts433_mcu },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, qnap_mcu_dt_ids);

static const struct mfd_cell qnap_mcu_subdevs[] = {
	{ .name = "qnap-mcu-input", },
	{ .name = "qnap-mcu-leds", },
	{ .name = "qnap-mcu-hwmon", }
};

static int qnap_mcu_probe(struct serdev_device *serdev)
{
	struct device *dev = &serdev->dev;
	struct qnap_mcu *mcu;
	int ret;

	mcu = devm_kzalloc(dev, sizeof(*mcu), GFP_KERNEL);
	if (!mcu)
		return -ENOMEM;

	mcu->serdev = serdev;
	dev_set_drvdata(dev, mcu);

	mcu->variant = of_device_get_match_data(dev);
	if (!mcu->variant)
		return -ENODEV;

	mutex_init(&mcu->bus_lock);
	mutex_init(&mcu->reply_lock);

	serdev_device_set_client_ops(serdev, &qnap_mcu_serdev_device_ops);
	ret = devm_serdev_device_open(dev, serdev);
	if (ret)
		return ret;

	serdev_device_set_baudrate(serdev, mcu->variant->baud_rate);
	serdev_device_set_flow_control(serdev, false);

	ret = serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);
	if (ret) {
		dev_err(dev, "Failed to set parity\n");
		return ret;
	}

	ret = qnap_mcu_get_version(mcu);
	if (ret)
		return ret;

	ret = devm_register_sys_off_handler(dev,
					    SYS_OFF_MODE_POWER_OFF_PREPARE,
					    SYS_OFF_PRIO_DEFAULT,
					    &qnap_mcu_power_off, mcu);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register poweroff handler\n");

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, qnap_mcu_subdevs,
				   ARRAY_SIZE(qnap_mcu_subdevs), NULL, 0, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "adding qnap mfd devices failed\n");

	return 0;
}

static struct serdev_device_driver qnap_mcu_drv = {
	.probe			= qnap_mcu_probe,
	.driver = {
		.name		= "qnap-mcu",
		.of_match_table	= qnap_mcu_dt_ids,
	},
};
module_serdev_device_driver(qnap_mcu_drv);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("QNAP MCU core driver");
