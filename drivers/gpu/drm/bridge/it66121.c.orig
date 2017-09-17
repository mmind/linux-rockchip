/*
 * Copyright (C) 2016 Atmel
 *		      Bo Shen <voice.shen@atmel.com>
 *
 * Authors:	      Bo Shen <voice.shen@atmel.com>
 *		      Boris Brezillon <boris.brezillon@free-electrons.com>
 *		      Wu, Songjun <Songjun.Wu@atmel.com>
 *
 *
 * Copyright (C) 2010-2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>

#include "it66121.h"

//FIXME: move to general edid header?
#define DDC_SEGMENT_ADDR		0x30

struct hdmi_data_info {
//	int vic;
	bool sink_is_hdmi;
	bool sink_has_audio;
/*	unsigned int enc_in_format;
	unsigned int enc_out_format;
	unsigned int colorimetry; */
};

struct it66121_i2c {
	struct i2c_adapter adap;
	struct mutex lock;
	u8 ddc_addr;
	u8 segment_addr;
};

struct it66121 {
	struct i2c_client *client;
	struct regmap *regmap;
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct gpio_desc *reset_gpio;

	struct regulator_bulk_data *supplies;
	unsigned int num_supplies;

	struct it66121_i2c *i2c;
	struct i2c_adapter *ddc;

	struct hdmi_data_info hdmi_data;
};

static inline struct it66121 *bridge_to_it66121(struct drm_bridge *bridge)
{
	return container_of(bridge, struct it66121, bridge);
}

static inline struct it66121 *connector_to_it66121(struct drm_connector *con)
{
	return container_of(con, struct it66121, connector);
}

static void it66121_reset(struct it66121 *it66121)
{
	if (!it66121->reset_gpio)
		return;

	gpiod_set_value(it66121->reset_gpio, 1);
	usleep_range(150, 200);
	gpiod_set_value(it66121->reset_gpio, 0);
}

static enum drm_connector_status
it66121_connector_detect(struct drm_connector *connector, bool force)
{
	struct it66121 *it66121 = connector_to_it66121(connector);
	unsigned int status;

	regmap_read(it66121->regmap, IT66121_SYS_STATUS0, &status);

	return (status & IT66121_SYS_STATUS0_HP_DETECT) ?
	       connector_status_connected : connector_status_disconnected;
}

static const struct drm_connector_funcs it66121_connector_funcs = {
	.detect = it66121_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int it66121_get_modes(struct drm_connector *connector)
{
	struct it66121 *it66121 = connector_to_it66121(connector);
	struct edid *edid;
	int ret = 0;

	if (!it66121->ddc)
		return 0;

	edid = drm_get_edid(connector, it66121->ddc);
	if (edid) {
		it66121->hdmi_data.sink_is_hdmi = drm_detect_hdmi_monitor(edid);
		it66121->hdmi_data.sink_has_audio = drm_detect_monitor_audio(edid);
		drm_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	return ret;
}

static enum drm_mode_status it66121_mode_valid(struct drm_connector *connector,
					       struct drm_display_mode *mode)
{
	/* TODO: check mode */

	return MODE_OK;
}

static const struct drm_connector_helper_funcs it66121_connector_helper_funcs = {
	.get_modes = it66121_get_modes,
	.mode_valid = it66121_mode_valid,
};

static void it66121_bridge_disable(struct drm_bridge *bridge)
{
	struct it66121 *it66121 = bridge_to_it66121(bridge);

/*	regmap_update_bits(it66121->regmap, it66121_SYS_CTRL_DATA,
			   it66121_SYS_CTRL_PWR_DWN,
			   it66121_SYS_CTRL_PWR_DWN); */
}

static void it66121_bridge_enable(struct drm_bridge *bridge)
{
	struct it66121 *it66121 = bridge_to_it66121(bridge);

/*	regmap_update_bits(it66121->regmap, it66121_PWR_STATE_CTRL,
			   it66121_AVI_POWER_STATE_MSK,
			   it66121_AVI_POWER_STATE_D(0));
	regmap_update_bits(it66121->regmap, it66121_SYS_CTRL_DATA,
			   it66121_SYS_CTRL_PWR_DWN, 0); */
}

static void it66121_bridge_mode_set(struct drm_bridge *bridge,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adj)
{
	struct it66121 *it66121 = bridge_to_it66121(bridge);
	struct regmap *regmap = it66121->regmap;
	u8 buf[HDMI_INFOFRAME_SIZE(AVI)];
	struct hdmi_avi_infoframe frame;
	int ret;

/*	buf[0] = adj->clock;
	buf[1] = adj->clock >> 8;
	buf[2] = adj->vrefresh;
	buf[3] = 0x00;
	buf[4] = adj->hdisplay;
	buf[5] = adj->hdisplay >> 8;
	buf[6] = adj->vdisplay;
	buf[7] = adj->vdisplay >> 8;
	buf[8] = it66121_TPI_CLK_RATIO_1X | it66121_TPI_AVI_PIXEL_REP_NONE |
		 it66121_TPI_AVI_PIXEL_REP_BUS_24BIT;
	buf[9] = it66121_TPI_AVI_INPUT_RANGE_AUTO |
		 it66121_TPI_AVI_INPUT_COLORSPACE_RGB;

	ret = regmap_bulk_write(regmap, it66121_TPI_VIDEO_DATA, buf, 10);
	if (ret)
		return;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame, adj, false);
	if (ret < 0) {
		DRM_ERROR("couldn't fill AVI infoframe\n");
		return;
	}

	ret = hdmi_avi_infoframe_pack(&frame, buf, sizeof(buf));
	if (ret < 0) {
		DRM_ERROR("failed to pack AVI infoframe: %d\n", ret);
		return;
	} */

	/* Do not send the infoframe header, but keep the CRC field. */
/*	regmap_bulk_write(regmap, it66121_TPI_AVI_INFOFRAME,
			  buf + HDMI_INFOFRAME_HEADER_SIZE - 1,
			  HDMI_AVI_INFOFRAME_SIZE + 1); */
}

static int it66121_bridge_attach(struct drm_bridge *bridge)
{
	struct it66121 *it66121 = bridge_to_it66121(bridge);
	struct drm_device *drm = bridge->dev;
	int ret;

	drm_connector_helper_add(&it66121->connector,
				 &it66121_connector_helper_funcs);

	if (!drm_core_check_feature(drm, DRIVER_ATOMIC)) {
		dev_err(&it66121->client->dev,
			"it66121 driver is only compatible with DRM devices supporting atomic updates\n");
		return -ENOTSUPP;
	}

	ret = drm_connector_init(drm, &it66121->connector,
				 &it66121_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;

	if (it66121->client->irq > 0)
		it66121->connector.polled = DRM_CONNECTOR_POLL_HPD;
	else
		it66121->connector.polled = DRM_CONNECTOR_POLL_CONNECT;

	drm_connector_attach_encoder(&it66121->connector, bridge->encoder);

	return 0;
}

static const struct drm_bridge_funcs it66121_bridge_funcs = {
	.attach = it66121_bridge_attach,
	.mode_set = it66121_bridge_mode_set,
	.disable = it66121_bridge_disable,
	.enable = it66121_bridge_enable,
};

static const struct regmap_range it66121_volatile_ranges[] = {
	{ .range_min = 0, .range_max = 0xff },
};

static const struct regmap_access_table it66121_volatile_table = {
	.yes_ranges = it66121_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(it66121_volatile_ranges),
};

static const struct regmap_config it66121_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &it66121_volatile_table,
	.cache_type = REGCACHE_NONE,
};

static irqreturn_t it66121_interrupt(int irq, void *data)
{
	struct it66121 *it66121 = data;
	unsigned int status = 0;

/*	regmap_read(it66121->regmap, it66121_INT_STATUS, &status);
	regmap_write(it66121->regmap, it66121_INT_STATUS, status);

	if ((status & it66121_HOTPLUG_EVENT) && it66121->bridge.dev)
		drm_helper_hpd_irq_event(it66121->bridge.dev);
*/
	return IRQ_HANDLED;
}

/*
 * There is no ddc-done interrupt, so we need to poll
 * the ddc status till it has finished
 */
static int it66121_ddc_i2c_wait_done(struct it66121 *it66121)
{
	unsigned int status;
	int i, ret;

	for (i = 0 ; i < 200 ; i++) {
		ret = regmap_read(it66121->regmap, IT66121_DDC_STATUS, &status);
		if (ret)
			return ret;

		if (status & IT66121_DDC_STATUS_DONE)
			return 0;

/*		if((status & IT66121_DDC_STATUS_ERROR)


 ||(HDMITX_ReadI2C_Byte(REG_TX_INT_STAT1) & B_TX_INT_DDC_BUS_HANG))
            {
                HDMITX_DEBUG_PRINTF(("Called hdmitx_AboutDDC()\n"));
                hdmitx_AbortDDC();
                return ER_FAIL ;
            } */
	}

	return -EAGAIN;
}

static int it66121_ddc_i2c_read(struct it66121 *it66121, struct i2c_msg *msgs)
{
	int length = msgs->len;
	u8 *buf = msgs->buf;
	int ret;

	if (msgs->addr != DDC_ADDR)
		return -EINVAL;

	ret = regmap_write(it66121->regmap, IT66121_DDC_HEADER, IT66121_DDC_HEADER_EDID);
	if (ret < 0)
		return ret;

	ret = regmap_write(it66121->regmap, IT66121_DDC_REQCOUNT, EDID_LENGTH);
	if (ret < 0)
		return ret;

	ret = regmap_write(it66121->regmap, IT66121_DDC_CMD, IT66121_DDC_CMD_EDID_READ);
	if (ret < 0)
		return ret;

	ret = it66121_ddc_i2c_wait_done(it66121);
	if (ret < 0)
		return ret;

	while (length--) {
		unsigned int val;

		ret = regmap_read(it66121->regmap, IT66121_DDC_READ_FIFO, &val);
		if (ret < 0)
			return ret;

		*buf++ = val;
	}

	return 0;
}

static int it66121_ddc_i2c_write(struct it66121 *it66121, struct i2c_msg *msgs)
{
	int ret;

	/*
	 * The DDC module only support read EDID message, so
	 * we assume that each word write to this i2c adapter
	 * should be the offset of EDID word address.
	 */
	if ((msgs->len != 1) ||
	    ((msgs->addr != DDC_ADDR) && (msgs->addr != DDC_SEGMENT_ADDR)))
		return -EINVAL;

	if (msgs->addr == DDC_SEGMENT_ADDR) {
		ret = regmap_write(it66121->regmap, IT66121_DDC_SEGMENT, msgs->buf[0]);
		if (ret < 0)
			return ret;
	}
	if (msgs->addr == DDC_ADDR) {
		ret = regmap_write(it66121->regmap, IT66121_DDC_REQOFFSET, msgs->buf[0]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int it66121_ddc_i2c_xfer(struct i2c_adapter *adap,
			      struct i2c_msg *msgs, int num)
{
	struct it66121 *it66121 = i2c_get_adapdata(adap);
	struct it66121_i2c *i2c = it66121->i2c;
	int i, ret = 0;

	mutex_lock(&i2c->lock);

	/* Enable DDC master access */
	ret = regmap_write(it66121->regmap, IT66121_DDC_MASTER, IT66121_DDC_MASTER_HOST /* | B_TX_MASTERDDC */);
	if (ret < 0)
		return ret;

	ret = regmap_write(it66121->regmap, IT66121_DDC_CMD, IT66121_DDC_CMD_FIFO_CLEAR);
	if (ret < 0)
		return ret;

	ret = it66121_ddc_i2c_wait_done(it66121);
	if (ret < 0)
		return ret;

	for (i = 0; i < num; i++) {
		dev_dbg(&it66121->client->dev, "xfer: num: %d/%d, len: %d, flags: %#x\n",
			i + 1, num, msgs[i].len, msgs[i].flags);

		if (msgs[i].flags & I2C_M_RD)
			ret = it66121_ddc_i2c_read(it66121, &msgs[i]);
		else
			ret = it66121_ddc_i2c_write(it66121, &msgs[i]);

		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;
//	else
//		do_ddc_abort operation

	mutex_unlock(&i2c->lock);

	return ret;
}

static u32 it66121_ddc_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm it66121_ddc_algorithm = {
	.master_xfer	= it66121_ddc_i2c_xfer,
	.functionality	= it66121_ddc_i2c_func,
};

static struct i2c_adapter *it66121_ddc_i2c_adapter(struct it66121 *it66121)
{
	struct i2c_adapter *adap;
	struct it66121_i2c *i2c;
	int ret;

	i2c = devm_kzalloc(&it66121->client->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return ERR_PTR(-ENOMEM);

	mutex_init(&i2c->lock);

	adap = &i2c->adap;
	adap->class = I2C_CLASS_DDC;
	adap->owner = THIS_MODULE;
	adap->dev.parent = &it66121->client->dev;
	adap->dev.of_node = it66121->client->dev.of_node;
	adap->algo = &it66121_ddc_algorithm;
	strlcpy(adap->name, "IT66121", sizeof(adap->name));
	i2c_set_adapdata(adap, it66121);

	ret = i2c_add_adapter(adap);
	if (ret) {
		dev_warn(&it66121->client->dev, "cannot add %s I2C adapter\n", adap->name);
		devm_kfree(&it66121->client->dev, i2c);
		return ERR_PTR(ret);
	}

	it66121->i2c = i2c;

	dev_info(&it66121->client->dev, "registered %s I2C bus driver\n", adap->name);

	return adap;
}

static const char * const it66121_supply_names[] = {
	"avcc12", /* analog frontend power */
	"dvdd12", /* digital frontend power */
	"ivdd12", /* digital logic power */
	"ovdd", /* I/O pin power (1.8, 2.5 or 3.3V) */
	"ovdd33", /* 5V-tolerant I/O power */
	"pvcc12", /* core PLL power */
	"pvcc33", /* core PLL power */
	"vcc33", /*internal ROM power */
};

static int it66121_init_regulators(struct it66121 *it66121)
{
	struct device *dev = &it66121->client->dev;
	unsigned int i;
	int ret;

	it66121->num_supplies = ARRAY_SIZE(it66121_supply_names);
	it66121->supplies = devm_kcalloc(dev, it66121->num_supplies,
				     sizeof(*it66121->supplies), GFP_KERNEL);
	if (!it66121->supplies)
		return -ENOMEM;

	for (i = 0; i < it66121->num_supplies; i++)
		it66121->supplies[i].supply = it66121_supply_names[i];

	ret = devm_regulator_bulk_get(dev, it66121->num_supplies,
					   it66121->supplies);
	if (ret)
		return ret;

	return regulator_bulk_enable(it66121->num_supplies, it66121->supplies);
}

static void it66121_uninit_regulators(void *data)
{
	struct it66121 *it66121 = data;

	regulator_bulk_disable(it66121->num_supplies, it66121->supplies);
}

static int it66121_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	unsigned int status = 0;
	struct it66121 *it66121;
	u8 chipid[4];
	int ret;

	it66121 = devm_kzalloc(dev, sizeof(*it66121), GFP_KERNEL);
	if (!it66121)
		return -ENOMEM;

	it66121->client = client;
	i2c_set_clientdata(client, it66121);

	it66121->regmap = devm_regmap_init_i2c(client, &it66121_regmap_config);
	if (IS_ERR(it66121->regmap))
		return PTR_ERR(it66121->regmap);

	it66121->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						      GPIOD_OUT_LOW);
	if (IS_ERR(it66121->reset_gpio)) {
		dev_err(dev, "Failed to retrieve/request reset gpio: %ld\n",
			PTR_ERR(it66121->reset_gpio));
		return PTR_ERR(it66121->reset_gpio);
	}

	ret = it66121_init_regulators(it66121);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(dev, it66121_uninit_regulators, it66121);
	if (ret < 0)
		return ret;

	it66121_reset(it66121);

	it66121->ddc = it66121_ddc_i2c_adapter(it66121);
	if (IS_ERR(it66121->ddc)) {
		ret = PTR_ERR(it66121->ddc);
		it66121->ddc = NULL;
		return ret;
	}

/*	ret = regmap_write(it66121->regmap, it66121_REG_TPI_RQB, 0x0);
	if (ret)
		return ret;

	ret = regmap_bulk_read(it66121->regmap, it66121_REG_CHIPID(0),
			       &chipid, 4);
	if (ret) {
		dev_err(dev, "regmap_read failed %d\n", ret);
		return ret;
	}

	if (chipid[0] != 0xb0) {
		dev_err(dev, "Invalid chipid: %02x (expecting 0xb0)\n",
			chipid[0]);
		return -EINVAL;
	} */

	/* Clear all pending interrupts */
//	regmap_read(it66121->regmap, IT66121_INT_STATUS, &status);
//	regmap_write(it66121->regmap, IT66121_INT_STATUS, status);

	if (client->irq > 0) {
//		regmap_write(it66121->regmap, it66121_INT_ENABLE,
//			     it66121_HOTPLUG_EVENT);

		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						it66121_interrupt,
						IRQF_ONESHOT, dev_name(dev),
						it66121);
		if (ret)
			return ret;
	}

	it66121->bridge.funcs = &it66121_bridge_funcs;
	it66121->bridge.of_node = dev->of_node;
	drm_bridge_add(&it66121->bridge);


	return 0;
}

static int it66121_remove(struct i2c_client *client)
{
	struct it66121 *it66121 = i2c_get_clientdata(client);

	drm_bridge_remove(&it66121->bridge);

	return 0;
}

static const struct of_device_id it66121_dt_ids[] = {
	{ .compatible = "ite,it66121", },
	{ }
};
MODULE_DEVICE_TABLE(of, it66121_dt_ids);

static const struct i2c_device_id it66121_i2c_ids[] = {
	{ "it66121", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, it66121_i2c_ids);

static struct i2c_driver it66121_driver = {
	.probe = it66121_probe,
	.remove = it66121_remove,
	.driver = {
		.name = "it66121",
		.of_match_table = it66121_dt_ids,
	},
	.id_table = it66121_i2c_ids,
};
module_i2c_driver(it66121_driver);

MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("IT66121 RGB -> HDMI bridges");
MODULE_LICENSE("GPL");
