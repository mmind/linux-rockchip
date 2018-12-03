// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Heiko Stuebner <heiko@sntech.de>
 *
 * based on beagleboard it66121 i2c encoder driver
 * Copyright (C) 2017 Baozhu Zuo <zuobaozhu@gmail.com>
 */
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/string.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>

#include "it66121.h"

struct a_reg_entry {
	u16 reg;
	u8 mask;
	u8 val;
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

u8 bCSCMtx_RGB2YUV_ITU601_16_235[] = {
	0x00, 0x80, 0x00,
	0xB2, 0x04, 0x65, 0x02, 0xE9, 0x00,
	0x93, 0x3C, 0x18, 0x04, 0x55, 0x3F,
	0x49, 0x3D, 0x9F, 0x3E, 0x18, 0x04
};

u8 bCSCMtx_RGB2YUV_ITU601_0_255[] = {
	0x10, 0x80, 0x10,
	0x09, 0x04, 0x0E, 0x02, 0xC9, 0x00,
	0x0F, 0x3D, 0x84, 0x03, 0x6D, 0x3F,
	0xAB, 0x3D, 0xD1, 0x3E, 0x84, 0x03
};

u8 bCSCMtx_RGB2YUV_ITU709_16_235[] = {
	0x00, 0x80, 0x00,
	0xB8, 0x05, 0xB4, 0x01, 0x94, 0x00,
	0x4a, 0x3C, 0x17, 0x04, 0x9F, 0x3F,
	0xD9, 0x3C, 0x10, 0x3F, 0x17, 0x04
};

u8 bCSCMtx_RGB2YUV_ITU709_0_255[] = {
	0x10, 0x80, 0x10,
	0xEa, 0x04, 0x77, 0x01, 0x7F, 0x00,
	0xD0, 0x3C, 0x83, 0x03, 0xAD, 0x3F,
	0x4B, 0x3D, 0x32, 0x3F, 0x83, 0x03
};

u8 bCSCMtx_YUV2RGB_ITU601_16_235[] = {
	0x00, 0x00, 0x00,
	0x00, 0x08, 0x6B, 0x3A, 0x50, 0x3D,
	0x00, 0x08, 0xF5, 0x0A, 0x02, 0x00,
	0x00, 0x08, 0xFD, 0x3F, 0xDA, 0x0D
};

u8 bCSCMtx_YUV2RGB_ITU601_0_255[] = {
	0x04, 0x00, 0xA7,
	0x4F, 0x09, 0x81, 0x39, 0xDD, 0x3C,
	0x4F, 0x09, 0xC4, 0x0C, 0x01, 0x00,
	0x4F, 0x09, 0xFD, 0x3F, 0x1F, 0x10
};

u8 bCSCMtx_YUV2RGB_ITU709_16_235[] = {
	0x00, 0x00, 0x00,
	0x00, 0x08, 0x55, 0x3C, 0x88, 0x3E,
	0x00, 0x08, 0x51, 0x0C, 0x00, 0x00,
	0x00, 0x08, 0x00, 0x00, 0x84, 0x0E
};

u8 bCSCMtx_YUV2RGB_ITU709_0_255[] = {
	0x04, 0x00, 0xA7,
	0x4F, 0x09, 0xBA, 0x3B, 0x4B, 0x3E,
	0x4F, 0x09, 0x57, 0x0E, 0x02, 0x00,
	0x4F, 0x09, 0xFE, 0x3F, 0xE8, 0x10
};

static struct a_reg_entry it66121_init_table[] = {

	{ IT66121_VIDEOPARAM_STATUS, 0x0e, 0x0c }, /* undocumented bits */

#ifdef NON_SEQUENTIAL_YCBCR422 // for ITE HDMIRX
	{ IT66121_TXFIFO_CTRL, IT66121_TXFIFO_CTRL_XP_STABLETIME_MASK | IT66121_TXFIFO_CTRL_XP_LOCK_CHK | IT66121_TXFIFO_CTRL_PLL_BUF_RST | IT66121_TXFIFO_CTRL_AUTO_RST | IT66121_TXFIFO_CTRL_IO_NONSEQ, IT66121_TXFIFO_CTRL_XP_STABLETIME_75US | IT66121_TXFIFO_CTRL_PLL_BUF_RST | IT66121_TXFIFO_CTRL_AUTO_RST | IT66121_TXFIFO_CTRL_IO_NONSEQ },
#else
	{ IT66121_TXFIFO_CTRL, IT66121_TXFIFO_CTRL_XP_STABLETIME_MASK | IT66121_TXFIFO_CTRL_XP_LOCK_CHK | IT66121_TXFIFO_CTRL_PLL_BUF_RST | IT66121_TXFIFO_CTRL_AUTO_RST | IT66121_TXFIFO_CTRL_IO_NONSEQ, IT66121_TXFIFO_CTRL_XP_STABLETIME_75US | IT66121_TXFIFO_CTRL_PLL_BUF_RST | IT66121_TXFIFO_CTRL_AUTO_RST },
#endif


/* strange undocumented hdcp stuff */
	{ 0xF8, 0xFF, 0xC3 },
	{ 0xF8, 0xFF, 0xA5 },
	{ IT66121_HDCP, 0x80, 0x80 },
	{ 0x37, 0x01, 0x00 },
	{ IT66121_HDCP, 0x80, 0 },
	{ 0xF8, 0xFF, 0xFF },

	{ IT66121_CLK_CTRL1, IT66121_CLK_CTRL1_PLL_MANUAL_MASK | IT66121_CLK_CTRL1_LOCK_DISABLE | IT66121_CLK_CTRL1_VDO_LATCH_EDGE, IT66121_CLK_CTRL1_PLL_MANUAL(1) | PCLKINV },

	{ IT66121_INT_MASK0, 0xFF, ~(IT66121_INT_MASK0_RX_SENSE | IT66121_INT_MASK0_HPD) },
	{ IT66121_INT_MASK1, 0xFF, ~(IT66121_INT_MASK1_KSVLIST_CHK | IT66121_INT_MASK1_AUTH_DONE | IT66121_INT_MASK1_AUTH_FAIL) },
	{ IT66121_INT_MASK2, 0xFF, ~(0x0) },

	{ IT66121_INT_CLR0, 0xFF, 0xFF },
	{ IT66121_INT_CLR1, 0xFF, 0xFF },
	{ IT66121_SYS_STATUS0, IT66121_SYS_STATUS0_CLEAR_AUD_CTS | IT66121_SYS_STATUS0_INTACTDONE, IT66121_SYS_STATUS0_CLEAR_AUD_CTS | IT66121_SYS_STATUS0_INTACTDONE },

	{ IT66121_INT_CLR0, 0xFF, 0x00 },
	{ IT66121_INT_CLR1, 0xFF, 0x00 },
	{ IT66121_SYS_STATUS0, IT66121_SYS_STATUS0_CLEAR_AUD_CTS, 0 },

	{ IT66121_AUDIO_CTRL1, 0x20, InvAudCLK },

	{ 0, 0, 0 }
};

static struct a_reg_entry it66121_default_video_table[] = {
	/* Config default output format.*/
	{ 0x72, 0xff, 0x00 },
	{ 0x70, 0xff, 0x00 },
#ifndef DEFAULT_INPUT_YCBCR
// GenCSC\RGB2YUV_ITU709_16_235.c
	{ 0x72, 0xFF, 0x02 },
	{ 0x73, 0xFF, 0x00 },
	{ 0x74, 0xFF, 0x80 },
	{ 0x75, 0xFF, 0x00 },
	{ 0x76, 0xFF, 0xB8 },
	{ 0x77, 0xFF, 0x05 },
	{ 0x78, 0xFF, 0xB4 },
	{ 0x79, 0xFF, 0x01 },
	{ 0x7A, 0xFF, 0x93 },
	{ 0x7B, 0xFF, 0x00 },
	{ 0x7C, 0xFF, 0x49 },
	{ 0x7D, 0xFF, 0x3C },
	{ 0x7E, 0xFF, 0x18 },
	{ 0x7F, 0xFF, 0x04 },
	{ 0x80, 0xFF, 0x9F },
	{ 0x81, 0xFF, 0x3F },
	{ 0x82, 0xFF, 0xD9 },
	{ 0x83, 0xFF, 0x3C },
	{ 0x84, 0xFF, 0x10 },
	{ 0x85, 0xFF, 0x3F },
	{ 0x86, 0xFF, 0x18 },
	{ 0x87, 0xFF, 0x04 },
#else
// GenCSC\YUV2RGB_ITU709_16_235.c
	{ 0x72, 0xFF, 0x03 },
	{ 0x73, 0xFF, 0x00 },
	{ 0x74, 0xFF, 0x80 },
	{ 0x75, 0xFF, 0x00 },
	{ 0x76, 0xFF, 0x00 },
	{ 0x77, 0xFF, 0x08 },
	{ 0x78, 0xFF, 0x53 },
	{ 0x79, 0xFF, 0x3C },
	{ 0x7A, 0xFF, 0x89 },
	{ 0x7B, 0xFF, 0x3E },
	{ 0x7C, 0xFF, 0x00 },
	{ 0x7D, 0xFF, 0x08 },
	{ 0x7E, 0xFF, 0x51 },
	{ 0x7F, 0xFF, 0x0C },
	{ 0x80, 0xFF, 0x00 },
	{ 0x81, 0xFF, 0x00 },
	{ 0x82, 0xFF, 0x00 },
	{ 0x83, 0xFF, 0x08 },
	{ 0x84, 0xFF, 0x00 },
	{ 0x85, 0xFF, 0x00 },
	{ 0x86, 0xFF, 0x87 },
	{ 0x87, 0xFF, 0x0E },
#endif
	// 2012/12/20 added by Keming's suggestion test
	{ 0x88, 0xF0, 0x00 },
	//~jauchih.tseng@ite.com.tw
	{ IT66121_SW_RST, IT66121_SW_RST_SOFT_VID, 0 },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_setHDMI_table[] = {
	/* Config default HDMI Mode */
	{ IT66121_HDMI_MODE, IT66121_HDMI_MODE_HDMI, IT66121_HDMI_MODE_HDMI },
	{ IT66121_AV_MUTE, IT66121_AV_MUTE_BLUE_SCR | IT66121_AV_MUTE_MUTE, IT66121_AV_MUTE_BLUE_SCR | IT66121_AV_MUTE_MUTE },
	{ IT66121_GENERAL_CTRL, IT66121_INFOFRM_REPEAT_PACKET | IT66121_INFOFRM_ENABLE_PACKET, IT66121_INFOFRM_REPEAT_PACKET | IT66121_INFOFRM_ENABLE_PACKET },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_setDVI_table[] = {
	/* Config default DVI Mode */
	{ IT66121_AVIINFO_DB1, 0xFF, 0x00 },
	{ IT66121_HDMI_MODE, IT66121_HDMI_MODE_HDMI, 0 },
	{ IT66121_AV_MUTE, IT66121_AV_MUTE_BLUE_SCR | IT66121_AV_MUTE_MUTE, IT66121_AV_MUTE_BLUE_SCR },
	{ IT66121_GENERAL_CTRL, IT66121_INFOFRM_REPEAT_PACKET | IT66121_INFOFRM_ENABLE_PACKET, 0 },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_default_AVI_info_table[] = {
	/* Config default avi infoframe */
	{ 0x158, 0xFF, 0x10 },
	{ 0x159, 0xFF, 0x08 },
	{ 0x15A, 0xFF, 0x00 },
	{ 0x15B, 0xFF, 0x00 },
	{ 0x15C, 0xFF, 0x00 },
	{ 0x15D, 0xFF, 0x57 },
	{ 0x15E, 0xFF, 0x00 },
	{ 0x15F, 0xFF, 0x00 },
	{ 0x160, 0xFF, 0x00 },
	{ 0x161, 0xFF, 0x00 },
	{ 0x162, 0xFF, 0x00 },
	{ 0x163, 0xFF, 0x00 },
	{ 0x164, 0xFF, 0x00 },
	{ 0x165, 0xFF, 0x00 },
	{ IT66121_AVI_INFOFRM_CTRL, IT66121_INFOFRM_REPEAT_PACKET | IT66121_INFOFRM_ENABLE_PACKET, IT66121_INFOFRM_REPEAT_PACKET | IT66121_INFOFRM_ENABLE_PACKET },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_default_audio_info_table[] = {
	/* Config default audio infoframe */
	{ 0x168, 0xFF, 0x00 },
	{ 0x169, 0xFF, 0x00 },
	{ 0x16A, 0xFF, 0x00 },
	{ 0x16B, 0xFF, 0x00 },
	{ 0x16C, 0xFF, 0x00 },
	{ 0x16D, 0xFF, 0x71 },
	{ IT66121_AUD_INFOFRM_CTRL, IT66121_INFOFRM_REPEAT_PACKET | IT66121_INFOFRM_ENABLE_PACKET, IT66121_INFOFRM_REPEAT_PACKET | IT66121_INFOFRM_ENABLE_PACKET },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_aud_CHStatus_LPCM_20bit_48Khz[] = {
	{ 0x133, 0xFF, 0x00 },
	{ 0x134, 0xFF, 0x18 },
	{ 0x135, 0xFF, 0x00 },
	{ 0x191, 0xFF, 0x00 },
	{ 0x192, 0xFF, 0x00 },
	{ 0x193, 0xFF, 0x01 },
	{ 0x194, 0xFF, 0x00 },
	{ 0x198, 0xFF, 0x02 },
	{ 0x199, 0xFF, 0xDA },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_AUD_SPDIF_2ch_24bit[] = {
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_GATE_TXCLK, 0 },
	{ IT66121_SW_RST, IT66121_SW_RST_SOFT_AUD | IT66121_SW_RST_AUDIO_FIFO, IT66121_SW_RST_AUDIO_FIFO },
	{ 0xE0, 0xFF, 0xD1 },
	{ 0xE1, 0xFF, 0x01 },
	{ 0xE2, 0xFF, 0xE4 },
	{ 0xE3, 0xFF, 0x10 },
	{ 0xE4, 0xFF, 0x00 },
	{ 0xE5, 0xFF, 0x00 },
	{ IT66121_SW_RST, IT66121_SW_RST_SOFT_AUD | IT66121_SW_RST_AUDIO_FIFO, 0 },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_AUD_I2S_2ch_24bit[] = {
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_GATE_TXCLK, 0 },
	{ IT66121_SW_RST, IT66121_SW_RST_SOFT_AUD | IT66121_SW_RST_AUDIO_FIFO, IT66121_SW_RST_AUDIO_FIFO },
	{ 0xE0, 0xFF, 0xC1 },
	{ 0xE1, 0xFF, 0x01 },
	{ 0xE2, 0xFF, 0xE4 },
	{ 0xE3, 0xFF, 0x00 },
	{ 0xE4, 0xFF, 0x00 },
	{ 0xE5, 0xFF, 0x00 },
	{ IT66121_SW_RST, IT66121_SW_RST_SOFT_AUD | IT66121_SW_RST_AUDIO_FIFO, 0 },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_default_audio_table[] = {
	/* Config default audio output format. */
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_GATE_IACLK, 0x00 },
	{ IT66121_SW_RST, IT66121_SW_RST_SOFT_AUD | IT66121_SW_RST_AUDIO_FIFO, IT66121_SW_RST_AUDIO_FIFO },
	{ 0xE0, 0xFF, 0xC1 },
	{ 0xE1, 0xFF, 0x01 },
	{ 0xE2, 0xFF, 0xE4 },
	{ 0xE3, 0xFF, 0x00 },
	{ 0xE4, 0xFF, 0x00 },
	{ 0xE5, 0xFF, 0x00 },
	{ 0x133, 0xFF, 0x00 },
	{ 0x134, 0xFF, 0x18 },
	{ 0x135, 0xFF, 0x00 },
	{ 0x191, 0xFF, 0x00 },
	{ 0x192, 0xFF, 0x00 },
	{ 0x193, 0xFF, 0x01 },
	{ 0x194, 0xFF, 0x00 },
	{ 0x198, 0xFF, 0x02 },
	{ 0x199, 0xFF, 0xDB },
	{ IT66121_SW_RST, IT66121_SW_RST_SOFT_AUD | IT66121_SW_RST_AUDIO_FIFO, 0x00 },
	{ 0, 0, 0 }
};

/*
 * Switch bank and update the bank information.
 * This should be run with the bank_mutex already held.
 */
static int it66121_set_bank(struct it66121 *priv, int bank)
{
	int ret;

	dev_warn(&priv->i2c->dev, "switching to bank %d\n", bank);
	ret = regmap_update_bits(priv->regmap, IT66121_SYS_STATUS1,
				 IT66121_SYS_STATUS1_BANK_MASK, bank);
	if (ret < 0)
		return ret;

	priv->cur_bank = bank;
	return ret;
}

/*
 * Select the correct bank for a register operation
 *
 * The registers are separated into three register areas:
 * Reg_000 - Reg_02F are accessible in any register bank.
 * Reg_030 - Reg_0FF are accessible in register bank0
 * Reg_130 - Reg_1BF are accessible in register bank1.
 *
 * Select the correct bank and return an adapted register number.
 * This should be run with the bank_mutex already held.
 *
 * returns new register index for regmap operation
 */
static int it66121_prepare_bank(struct it66121 *priv, int *reg)
{
	int bank, ret;

	if (*reg >= 0x1bf) {
		dev_err(&priv->i2c->dev, "register 0x%x out of bounds\n", *reg);
		return -EINVAL;
	}

	/* accessible from any bank */
	if (*reg < 0x30)
		return 0;

	if (*reg >= 0x30 && *reg <= 0xff)
		bank = 0;

	if (*reg >= 0x130 && *reg <= 0x1bf) {
		bank = 1;
		*reg -= 0x100;
	}

	/* switch bank if needed */
	if (bank != priv->cur_bank) {
		ret = it66121_set_bank(priv, bank);
		if (ret < 0) {
			dev_err(&priv->i2c->dev,
				"bank switch failed %d\n", ret);
			return ret;
		}
	}

	return 0;
}

int it66121_reg_read(struct it66121 *priv, int reg)
{
	unsigned int val;
	int ret;

	mutex_lock(&priv->bank_mutex);

	ret = it66121_prepare_bank(priv, &reg);
	if (ret < 0)
		goto out;

	ret = regmap_read(priv->regmap, reg, &val);
if (reg < 0x10 || reg >= 0x20)
printk("it66121: read 0x%x from 0x%x, ret %d\n", val, reg, ret);
	if (ret < 0)
		goto out;
	ret = val;
out:
	mutex_unlock(&priv->bank_mutex);
	return ret;
}

int it66121_reg_write(struct it66121 *priv, int reg, u8 val)
{
	int ret;

	mutex_lock(&priv->bank_mutex);

	ret = it66121_prepare_bank(priv, &reg);
	if (ret < 0)
		goto out;

	ret = regmap_write(priv->regmap, reg, val);
if (reg < 0x10 || reg >= 0x20)
printk("it66121: wrote 0x%x to 0x%x, ret %d\n", val, reg, ret);
out:
	mutex_unlock(&priv->bank_mutex);
	return ret;
}

int it66121_reg_update_bits(struct it66121 *priv, unsigned int reg,
			    u8 mask, u8 val)
{
	int ret;

	mutex_lock(&priv->bank_mutex);

	ret = it66121_prepare_bank(priv, &reg);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(priv->regmap, reg, mask, val);
if (reg < 0x10 || reg >= 0x20)
printk("it66121: update 0x%x/0x%x in 0x%x, ret %d\n", val, mask, reg, ret);
out:
	mutex_unlock(&priv->bank_mutex);
	return ret;
}

int it66121_reg_bulk_write(struct it66121 *priv, unsigned int reg,
				  const void *val, size_t val_count)
{
	int ret;

	/* catch bulk writes across banks */
	if (reg < 0x130 && reg + val_count >= 0x130) {
		dev_err(&priv->i2c->dev, "bulk-write across bank boundary\n");
		return -EINVAL;
	}

	mutex_lock(&priv->bank_mutex);

	ret = it66121_prepare_bank(priv, &reg);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_write(priv->regmap, reg, val, val_count);
if (reg < 0x10 || reg >= 0x20)
printk("it66121: writing to %d registers at 0x%x, ret %d\n", val_count, reg, ret);
out:
	mutex_unlock(&priv->bank_mutex);
	return ret;
}

static int it66121_load_reg_table(struct it66121 *priv, struct a_reg_entry table[])
{
	int ret = 0;
	int i;
	for (i = 0;; i++) {
		if (table[i].reg == 0 && table[i].mask == 0 && table[i].val == 0) {
			return ret;
		} else if (table[i].mask == 0 && table[i].val == 0) {
			mdelay(table[i].reg);
		} else if (table[i].mask == 0xFF) {
			ret = it66121_reg_write(priv, table[i].reg, table[i].val);
		} else {
			ret = it66121_reg_update_bits(priv, table[i].reg, table[i].mask, table[i].val);
		}
		if (ret < 0) {
			return ret;
		}
	}
	return ret;
}



static enum drm_connector_status
it66121_connector_detect(struct drm_connector *connector, bool force)
{
	struct it66121 *priv = connector_to_it66121(connector);
	char isconnect;

	if (WARN_ON(pm_runtime_get_sync(&priv->i2c->dev) < 0))
		return connector_status_unknown;

	isconnect = it66121_reg_read(priv, IT66121_SYS_STATUS0);
	isconnect &= IT66121_SYS_STATUS0_HP_DETECT;

	pm_runtime_put(&priv->i2c->dev);
	return isconnect ? connector_status_connected
			 : connector_status_disconnected;
}

static const struct drm_connector_funcs it66121_connector_funcs = {
	.detect = it66121_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};


static void it66121_abort_DDC(struct it66121 *priv)
{
	u8 CPDesire, SWReset, DDCMaster;
	u8 uc, timeout, i;
	// save the SW reset,DDC master,and CP Desire setting.
	SWReset = it66121_reg_read(priv, IT66121_SW_RST);
	CPDesire = it66121_reg_read(priv, IT66121_HDCP);
	DDCMaster = it66121_reg_read(priv, IT66121_DDC_MASTER);

	it66121_reg_write(priv, IT66121_HDCP, CPDesire & (IT66121_HDCP_DESIRED));
	it66121_reg_write(priv, IT66121_SW_RST, SWReset | IT66121_SW_RST_HDCP);
	it66121_reg_write(priv, IT66121_DDC_MASTER, IT66121_DDC_MASTER_DDC | IT66121_DDC_MASTER_HOST);

	// 2009/01/15 modified by Jau-Chih.Tseng@ite.com.tw
	// do abort DDC twice.
	for (i = 0; i < 2; i++) {
		it66121_reg_write(priv, IT66121_DDC_CMD, IT66121_DDC_CMD_DDC_ABORT);

		for (timeout = 0; timeout < 200; timeout++) {
			uc = it66121_reg_read(priv, IT66121_DDC_STATUS);
			if (uc & IT66121_DDC_STATUS_DONE) {
				break; // success
			}
			if (uc & (IT66121_DDC_STATUS_NOACK | IT66121_DDC_STATUS_WAIT_BUS | IT66121_DDC_STATUS_ARBILOSE)) {
				dev_err(&priv->i2c->dev, "it66121_abort_DDC Fail by reg16=%02X\n", (int)uc);
				break;
			}
			mdelay(1); // delay 1 ms to stable.
		}
	}
	//~Jau-Chih.Tseng@ite.com.tw
}

/*
 * To get the EDID data, DDC master should write segment with I2C address 0x60 then ask
 * the bytes with I2C address 0xA0. (That is the major difference to burst read.) The
 * programming of EDID read should set the following registers:
 *
 * Reg11 \A8C Should set 0xA0 for EDID fetching.
 * Reg12 \A8C Set the starting offset of EDID block on current segment.
 * Reg13 \A8C Set the number of byte to read back. The data will be put in DDC FIFO,
 * 		   therefore, cannot exceed the size (32) of FIFO.
 * Reg14 \A8C The segment of EDID block to read.
 * Reg15 \A8C DDC command should be 0x03.
 *
 * After reg15 written 0x03, the command is fired and successfully when reg16[7] = '1' or
 * fail by reg16[5:3] contains any bit '1'. When EDID read done, EDID can be read from DDC
 * FIFO.
 * Note: By hardware implementation, the I2C access sequence on PCSCL/PCSDA should be
 *
 * <start>-<0x98/0x9A>-<0x17>-<Restart>-<0x99/0x9B>-<read data>-<stop>
 *
 * If the sequence is the following sequence, the FIFO read will be fail.
 *
 * <start>-<0x98/0x9A>-<0x17>-<stop>-<start>-<0x99/0x9B>-<read data>-<stop>
 */
static int it66121_read_edid_block(void *data, u8 *buf, unsigned int blk, size_t length)
{
	struct it66121 *priv = data;
	u16 ReqCount;
	u8 bCurrOffset;
	u16 TimeOut;
	u8 *pBuff = buf;
	u8 ucdata;
	u8 bSegment;

	if (!buf)
		return -EINVAL;

	if (it66121_reg_read(priv, IT66121_INT_STAT0) & IT66121_INT_STAT0_DDC_BUS_HANG) {
		dev_err(&priv->i2c->dev, "Sorry, ddc bus is hang\n");
		it66121_abort_DDC(priv);
	}

	/*clear the DDC FIFO*/
	it66121_reg_write(priv, IT66121_DDC_MASTER, IT66121_DDC_MASTER_DDC | IT66121_DDC_MASTER_HOST);
	it66121_reg_write(priv, IT66121_DDC_CMD, IT66121_DDC_CMD_FIFO_CLEAR);

	bCurrOffset = (blk % 2) / length;
	bSegment  = blk / length;

	while (length > 0) {
		ReqCount = (length > DDC_FIFO_MAXREQ) ? DDC_FIFO_MAXREQ : length;

		it66121_reg_write(priv, IT66121_DDC_MASTER, IT66121_DDC_MASTER_DDC | IT66121_DDC_MASTER_HOST);
		it66121_reg_write(priv, IT66121_DDC_CMD, IT66121_DDC_CMD_FIFO_CLEAR);

		for (TimeOut = 0; TimeOut < 200; TimeOut++) {
			ucdata = it66121_reg_read(priv, IT66121_DDC_STATUS);

			if (ucdata & IT66121_DDC_STATUS_DONE)
				break;

			if ((ucdata & IT66121_DDC_STATUS_ERROR) || (it66121_reg_read(priv, IT66121_INT_STAT0) & IT66121_INT_STAT0_DDC_BUS_HANG)) {
				dev_err(&priv->i2c->dev, "it66121_read_edid_block(): DDC_STATUS = %02X,fail.\n", (int)ucdata);
				/*clear the DDC FIFO*/
				it66121_reg_write(priv, IT66121_DDC_MASTER, IT66121_DDC_MASTER_DDC | IT66121_DDC_MASTER_HOST);
				it66121_reg_write(priv, IT66121_DDC_CMD, IT66121_DDC_CMD_FIFO_CLEAR);
				return -ENXIO;
			}
		}

		it66121_reg_write(priv, IT66121_DDC_MASTER, IT66121_DDC_MASTER_DDC | IT66121_DDC_MASTER_HOST);
		it66121_reg_write(priv, IT66121_DDC_HEADER, DDC_EDID_ADDRESS);
		it66121_reg_write(priv, IT66121_DDC_REQOFFSET, bCurrOffset);
		it66121_reg_write(priv, IT66121_DDC_REQCOUNT, (u8)ReqCount);
		it66121_reg_write(priv, IT66121_DDC_SEGMENT, bSegment);
		it66121_reg_write(priv, IT66121_DDC_CMD, IT66121_DDC_CMD_EDID_READ);

		bCurrOffset += ReqCount;
		length -= ReqCount;

		for (TimeOut = 250; TimeOut > 0; TimeOut--) {
			mdelay(1);
			ucdata = it66121_reg_read(priv, IT66121_DDC_STATUS);
			if (ucdata & IT66121_DDC_STATUS_DONE)
				break;

			if (ucdata & IT66121_DDC_STATUS_ERROR) {
				dev_err(&priv->i2c->dev, "it66121_read_edid_block(): DDC_STATUS = %02X,fail.\n", (int)ucdata);
				return -1;
			}
		}

		if (TimeOut == 0) {
			dev_err(&priv->i2c->dev, "it66121_read_edid_block(): DDC TimeOut %d . \n", (int)ucdata);
			return -1;
		}

		do {
			*(pBuff++) = it66121_reg_read(priv, IT66121_DDC_READ_FIFO);
			ReqCount--;
		} while (ReqCount > 0);

	}

	return 0;
}

static int it66121_connector_get_modes(struct drm_connector *connector)
{
	struct it66121 *priv = connector_to_it66121(connector);
	struct edid *edid;
	int ret;

	ret = pm_runtime_get_sync(&priv->i2c->dev);
	if (WARN_ON(ret < 0))
		return ret;

	edid = drm_do_get_edid(connector, it66121_read_edid_block, priv);
	if (!edid) {
		pm_runtime_put(&priv->i2c->dev);
		return -ENODEV;
	}

	priv->dvi_mode = !drm_detect_hdmi_monitor(edid);

	drm_connector_update_edid_property(connector, edid);
//	cec_notifier_set_phys_addr_from_edid(hdata->notifier, edid);

	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);
	pm_runtime_put(&priv->i2c->dev);
	return ret;
}

static int it66121_connector_mode_valid(struct drm_connector *connector,
										struct drm_display_mode *mode)
{
	//return drm_match_cea_mode(mode) == 0 ? MODE_BAD : MODE_OK;
	return MODE_OK;
}

static const struct drm_connector_helper_funcs it66121_connector_helper_funcs = {
	.get_modes = it66121_connector_get_modes,
	.mode_valid = it66121_connector_mode_valid,
};

static void it66121_set_CSC_scale(struct it66121 *priv,
				  u8 input_color_mode)
{
	u8 csc = 0;
	u8 filter = 0;
	int i;

	switch (input_color_mode & F_MODE_CLRMOD_MASK) {
	case F_MODE_YUV444:
		switch (OUTPUT_COLOR_MODE & F_MODE_CLRMOD_MASK) {
		case F_MODE_YUV444:
			csc = IT66121_CSC_CTRL_CSC_BYPASS;
			break;
		case F_MODE_YUV422:
			csc = IT66121_CSC_CTRL_CSC_BYPASS;
			if (input_color_mode & F_VIDMODE_EN_UDFILT) // YUV444 to YUV422 need up/down filter for processing.
				filter |= IT66121_CSC_CTRL_UDFILTER;
			break;
		case F_MODE_RGB444:
			csc = IT66121_CSC_CTRL_CSC_YUV2RGB;
			if (input_color_mode & F_VIDMODE_EN_DITHER) // YUV444 to RGB24 need dither
				filter |= IT66121_CSC_CTRL_DITHER | IT66121_CSC_CTRL_DNFREE_GO;
			break;
		}
		break;
	case F_MODE_YUV422:
		switch (OUTPUT_COLOR_MODE & F_MODE_CLRMOD_MASK) {
		case F_MODE_YUV444:
			csc = IT66121_CSC_CTRL_CSC_BYPASS;
			if (input_color_mode & F_VIDMODE_EN_UDFILT) // YUV422 to YUV444 need up filter
				filter |= IT66121_CSC_CTRL_UDFILTER;
			if (input_color_mode & F_VIDMODE_EN_DITHER) // YUV422 to YUV444 need dither
				filter |= IT66121_CSC_CTRL_DITHER | IT66121_CSC_CTRL_DNFREE_GO;
			break;
		case F_MODE_YUV422:
			csc = IT66121_CSC_CTRL_CSC_BYPASS;
			break;
		case F_MODE_RGB444:
			csc = IT66121_CSC_CTRL_CSC_YUV2RGB;
			if (input_color_mode & F_VIDMODE_EN_UDFILT) // YUV422 to RGB24 need up/dn filter.
				filter |= IT66121_CSC_CTRL_UDFILTER;
			if (input_color_mode & F_VIDMODE_EN_DITHER) // YUV422 to RGB24 need dither
				filter |= IT66121_CSC_CTRL_DITHER | IT66121_CSC_CTRL_DNFREE_GO;
			break;
		}
		break;
	case F_MODE_RGB444:
		switch (OUTPUT_COLOR_MODE & F_MODE_CLRMOD_MASK) {
		case F_MODE_YUV444:
			csc = IT66121_CSC_CTRL_CSC_RGB2YUV;
			if (INPUT_COLOR_MODE & F_VIDMODE_EN_DITHER) // RGB24 to YUV444 need dither
				filter |= IT66121_CSC_CTRL_DITHER | IT66121_CSC_CTRL_DNFREE_GO;
			break;
		case F_MODE_YUV422:
			if (input_color_mode & F_VIDMODE_EN_UDFILT) // RGB24 to YUV422 need down filter.
				filter |= IT66121_CSC_CTRL_UDFILTER;
			if (input_color_mode & F_VIDMODE_EN_DITHER) // RGB24 to YUV422 need dither
				filter |= IT66121_CSC_CTRL_DITHER | IT66121_CSC_CTRL_DNFREE_GO;
			csc = IT66121_CSC_CTRL_CSC_RGB2YUV;
			break;
		case F_MODE_RGB444:
			csc = IT66121_CSC_CTRL_CSC_BYPASS;
			break;
		}
		break;
	}

	if (csc == IT66121_CSC_CTRL_CSC_RGB2YUV) {
		switch (input_color_mode & (F_VIDMODE_ITU709 | F_VIDMODE_16_235)) {
		case F_VIDMODE_ITU709 | F_VIDMODE_16_235:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				it66121_reg_write(priv, IT66121_CSC_Y_OFFSET + i, bCSCMtx_RGB2YUV_ITU709_16_235[i]);
			break;
		case F_VIDMODE_ITU709 | F_VIDMODE_0_255:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				it66121_reg_write(priv, IT66121_CSC_Y_OFFSET + i, bCSCMtx_RGB2YUV_ITU709_0_255[i]);
			break;
		case F_VIDMODE_ITU601 | F_VIDMODE_16_235:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				it66121_reg_write(priv, IT66121_CSC_Y_OFFSET + i, bCSCMtx_RGB2YUV_ITU601_16_235[i]);
			break;
		case F_VIDMODE_ITU601 | F_VIDMODE_0_255:
		default:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				it66121_reg_write(priv, IT66121_CSC_Y_OFFSET + i, bCSCMtx_RGB2YUV_ITU601_0_255[i]);
			break;
		}
	}

	if (csc == IT66121_CSC_CTRL_CSC_YUV2RGB) {
		switch (input_color_mode & (F_VIDMODE_ITU709 | F_VIDMODE_16_235)) {
		case F_VIDMODE_ITU709 | F_VIDMODE_16_235:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				it66121_reg_write(priv, IT66121_CSC_Y_OFFSET + i, bCSCMtx_YUV2RGB_ITU709_16_235[i]);
			break;
		case F_VIDMODE_ITU709 | F_VIDMODE_0_255:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				it66121_reg_write(priv, IT66121_CSC_Y_OFFSET + i, bCSCMtx_YUV2RGB_ITU709_0_255[i]);
			break;
		case F_VIDMODE_ITU601 | F_VIDMODE_16_235:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				it66121_reg_write(priv, IT66121_CSC_Y_OFFSET + i, bCSCMtx_YUV2RGB_ITU601_16_235[i]);
			break;
		case F_VIDMODE_ITU601 | F_VIDMODE_0_255:
		default:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				it66121_reg_write(priv, IT66121_CSC_Y_OFFSET + i, bCSCMtx_YUV2RGB_ITU601_0_255[i]);
			break;
		}
	}

	priv->need_csc = (csc != IT66121_CSC_CTRL_CSC_BYPASS);
	it66121_reg_update_bits(priv, IT66121_CSC_CTRL, IT66121_CSC_CTRL_CSC_MASK | IT66121_CSC_CTRL_DNFREE_GO | IT66121_CSC_CTRL_DITHER | IT66121_CSC_CTRL_UDFILTER, filter | csc);
}


static int it66121_afe_enable(struct it66121 *priv)
{
	int ret;

dev_err(&priv->i2c->dev, "%s", __func__);
	/* power up AFE */
	ret = it66121_reg_update_bits(priv, IT66121_AFE_DRV_CTRL,
				      IT66121_AFE_DRV_CTRL_PWD, 0);
	if (ret < 0)
		return ret;

	ret = it66121_reg_update_bits(priv, IT66121_AFE_XP_CTRL,
				      IT66121_AFE_XP_CTRL_PWDPLL |
				      IT66121_AFE_XP_CTRL_PWDI, 0);
	if (ret < 0)
		return ret;

	ret = it66121_reg_update_bits(priv, IT66121_AFE_IP_CTRL, IT66121_AFE_IP_CTRL_PWDPLL, 0);

	msleep(100);

	/* pull AFE out of reset */
	it66121_reg_update_bits(priv, IT66121_AFE_DRV_CTRL, IT66121_AFE_DRV_CTRL_RST, 0);
	it66121_reg_update_bits(priv, IT66121_AFE_XP_CTRL, IT66121_AFE_XP_CTRL_RESETB, IT66121_AFE_XP_CTRL_RESETB);
	it66121_reg_update_bits(priv, IT66121_AFE_IP_CTRL, IT66121_AFE_IP_CTRL_RESETB, IT66121_AFE_IP_CTRL_RESETB);

	return 0;
}

static int it66121_afe_disable(struct it66121 *priv)
{
dev_err(&priv->i2c->dev, "%s", __func__);
	/* put AFE in reset */
	it66121_reg_update_bits(priv, IT66121_AFE_DRV_CTRL, IT66121_AFE_DRV_CTRL_RST, IT66121_AFE_DRV_CTRL_RST);
	it66121_reg_update_bits(priv, IT66121_AFE_XP_CTRL, IT66121_AFE_XP_CTRL_RESETB, 0);
	it66121_reg_update_bits(priv, IT66121_AFE_IP_CTRL, IT66121_AFE_IP_CTRL_RESETB, 0);

	msleep(100);

	/* power down AFE */
	it66121_reg_update_bits(priv, IT66121_AFE_DRV_CTRL, IT66121_AFE_DRV_CTRL_PWD, IT66121_AFE_DRV_CTRL_PWD);
	it66121_reg_update_bits(priv, IT66121_AFE_XP_CTRL, IT66121_AFE_XP_CTRL_PWDPLL | IT66121_AFE_XP_CTRL_PWDI, IT66121_AFE_XP_CTRL_PWDPLL | IT66121_AFE_XP_CTRL_PWDI);
	it66121_reg_update_bits(priv, IT66121_AFE_IP_CTRL, IT66121_AFE_IP_CTRL_PWDPLL, IT66121_AFE_IP_CTRL_PWDPLL);

	return 0;
}

static void it66121_afe_setup(struct it66121 *priv, bool high_level)
{
	int ret;
//	it66121_reg_write(priv, IT66121_AFE_DRV_CTRL, IT66121_AFE_DRV_CTRL_RST);

dev_err(&priv->i2c->dev, "%s", __func__);
	ret = it66121_reg_update_bits(priv, IT66121_AFE_XP_CTRL,
				      IT66121_AFE_XP_CTRL_GAIN |
				      IT66121_AFE_XP_CTRL_ER0,
				      high_level ? IT66121_AFE_XP_CTRL_GAIN :
						   IT66121_AFE_XP_CTRL_ER0);

	ret = it66121_reg_update_bits(priv, IT66121_AFE_IP_CTRL,
				      IT66121_AFE_IP_CTRL_GAIN |
				      IT66121_AFE_IP_CTRL_ER0 |
				      IT66121_AFE_IP_CTRL_EC1,
				      high_level ? IT66121_AFE_IP_CTRL_GAIN :
						   IT66121_AFE_IP_CTRL_ER0 |
						   IT66121_AFE_IP_CTRL_EC1);

	ret = it66121_reg_update_bits(priv, IT66121_AFE_XPIP_PARAM,
				      IT66121_AFE_XPIP_PARAM_XP_EC1,
				      high_level ? 0 :
						IT66121_AFE_XPIP_PARAM_XP_EC1);

//	it66121_reg_write(priv, IT66121_AFE_DRV_CTRL, 0);
}

/**
 * To enable the video of IT66121, the input signal type and
 * output TMDS should be programmed. The following sequence is to
 * set the video mode:
 * 1. Set regC1[0] = '1' for AVMUTE the output.
 * 2. Programming Input Signal Type
 * 3. Set color space converting by the input color space and output color space.
 * 4. Set AFE by the input video pixel clock.
 * 5. Set HDMI package or DVI mode.
 * 6. Set HDCP if necessary.
 * 7. Set Audio if necessary.
 * 8. Clear the AVMUTE by regC1[0] = '1' and regC6 = 0x03.
 */
static void it66121_enable_video_output(struct it66121 *priv,
					struct drm_display_mode *mode)
{
	u8 is_high_clk = 0;
	u8 pixelrep = 0;
	u8 input_color_mode = F_MODE_RGB444;
	u8 Colorimetry = 0;
	u8 udata;
//	u8 vic;

//	vic = drm_match_cea_mode(mode);

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		pixelrep = 1;

	if ((pixelrep + 1) * (mode->clock * 1000) > 80000000L)
		is_high_clk = 1;

	if (mode->hdisplay * 9 == mode->vdisplay * 16) {
		Colorimetry = HDMI_COLORIMETRY_ITU_709;
	}

	if (mode->hdisplay * 3 == mode->vdisplay * 4) {
		Colorimetry = HDMI_COLORIMETRY_ITU_601;
	}

	if (Colorimetry == HDMI_COLORIMETRY_ITU_709) {
		input_color_mode |= F_VIDMODE_ITU709;
	} else {
		input_color_mode &= ~F_VIDMODE_ITU709;
	}

	if (pixelrep == 0 && mode->hdisplay == 640 &&
		mode->vdisplay == 480 && mode->vrefresh == 60) {
		input_color_mode |= F_VIDMODE_16_235;
	} else {
		input_color_mode &= ~F_VIDMODE_16_235;
	}

	/* undocumented */
	it66121_reg_write(priv, IT66121_PLL_CTRL, is_high_clk ? 0x30 : 0x00);

	it66121_reg_write(priv, IT66121_SW_RST, IT66121_SW_RST_SOFT_VID |
			  IT66121_SW_RST_AUDIO_FIFO |
			  IT66121_SW_RST_SOFT_AUD |
			  IT66121_SW_RST_HDCP);

	// 2009/12/09 added by jau-chih.tseng@ite.com.tw
//	it66121_reg_write(priv, IT66121_AVIINFO_DB1, 0x00);
	//~jau-chih.tseng@ite.com.tw

	/*Set regC1[0] = '1' for AVMUTE the output, only support hdmi mode now*/
	//it66121_reg_update_bits(priv, IT66121_AV_MUTE, IT66121_AV_MUTE_MUTE, IT66121_AV_MUTE_MUTE);
	it66121_reg_update_bits(priv, IT66121_AV_MUTE, IT66121_AV_MUTE_MUTE,  0);
	it66121_reg_write(priv, IT66121_GENERAL_CTRL, IT66121_INFOFRM_ENABLE_PACKET | IT66121_INFOFRM_REPEAT_PACKET);

	/* Programming Input Signal Type
	 * InputColorMode: F_MODE_RGB444
	 *				   F_MODE_YUV422
	 *  			   F_MODE_YUV444
	 *
	 *bInputSignalType: T_MODE_PCLKDIV2
	 *  				T_MODE_CCIR656
	 *  				T_MODE_SYNCEMB
	 *  				T_MODE_INDDR
	*/
	udata = it66121_reg_read(priv, IT66121_INPUT_MODE);
	udata &= ~(IT66121_INPUT_MODE_COLOR_MASK | IT66121_INPUT_MODE_CCIR656 | IT66121_INPUT_MODE_SYNC_EMBEDDED | IT66121_INPUT_MODE_DDR | IT66121_INPUT_MODE_PCLKDIV2);
	udata |= IT77121_INPUT_MODE_PCLK_DLY(1); //input clock delay 1 for 1080P DDR
	udata |= input_color_mode & IT66121_INPUT_MODE_COLOR_MASK; //define in it66121.h, F_MODE_RGB444
	udata |= INPUT_SIGNAL_TYPE; //define in it66121.h,  24 bit sync seperate
	it66121_reg_write(priv, IT66121_INPUT_MODE, udata);

	/*
	 * Set color space converting by the input color space and output color space.
	*/
	it66121_set_CSC_scale(priv, input_color_mode);
#ifdef INVERT_VID_LATCHEDGE
	udata = it66121_reg_read(priv, IT66121_CLK_CTRL1);
	udata |= IT66121_CLK_CTRL1_VDO_LATCH_EDGE;
	it66121_reg_write(priv, IT66121_CLK_CTRL1, udata);
#endif

	it66121_afe_setup(priv, is_high_clk); // pass if High Freq request
/*	it66121_reg_write(priv, IT66121_SW_RST, IT66121_SW_RST_AUDIO_FIFO |
			  IT66121_SW_RST_SOFT_AUD |
			  IT66121_SW_RST_HDCP);*/
}

static void it66121_bridge_mode_set(struct drm_bridge *bridge,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adj)
{
	struct it66121 *priv = bridge_to_it66121(bridge);
	struct hdmi_avi_infoframe frame;
	u8 buf[HDMI_INFOFRAME_SIZE(AVI)];
	int ret;

	ret = it66121_reg_write(priv, IT66121_HDMI_MODE,
				priv->dvi_mode ? 0 : IT66121_HDMI_MODE_HDMI);
	if (ret < 0) {
		DRM_ERROR("failed to set hdmi mode\n");
		return;
	}

//FIXME: possibly just set AVIINFO_DB1 to 0 in the DVI case?
	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame, adj, false);
	if (ret < 0) {
		DRM_ERROR("couldn't fill AVI infoframe\n");
		return;
	}

	ret = hdmi_avi_infoframe_pack(&frame, buf, sizeof(buf));
	if (ret < 0) {
		DRM_ERROR("failed to pack AVI infoframe: %d\n", ret);
		return;
	}

	/* register layout is DB1..DB5..CHK..DB6.. requiring 2 writes */
	ret = it66121_reg_bulk_write(priv, IT66121_AVIINFO_DB1,
				     buf + HDMI_INFOFRAME_HEADER_SIZE, 5);
	if (ret < 0) {
		DRM_ERROR("failed to write AVI infoframe: %d\n", ret);
		return;
	}

	ret = it66121_reg_bulk_write(priv, IT66121_AVIINFO_DB6,
				     buf + HDMI_INFOFRAME_HEADER_SIZE + 5,
				     HDMI_AVI_INFOFRAME_SIZE - 5);
	if (ret < 0) {
		DRM_ERROR("failed to write AVI infoframe: %d\n", ret);
		return;
	}

	ret = it66121_reg_write(priv, IT66121_AVIINFO_SUM, buf[3]);
	if (ret < 0) {
		DRM_ERROR("failed to write AVI infoframe: %d\n", ret);
		return;
	}

	it66121_enable_video_output(priv, adj);
}

static void it66121_bridge_disable(struct drm_bridge *bridge)
{
	struct it66121 *priv = bridge_to_it66121(bridge);
	int ret;

printk("%s: disabling bridge\n", __func__);
	/* disable video output */
//FIXME: simply do avmute?
	it66121_reg_update_bits(priv, IT66121_SW_RST, IT66121_SW_RST_SOFT_VID, IT66121_SW_RST_SOFT_VID);

	it66121_reg_write(priv, IT66121_AVI_INFOFRM_CTRL, 0);

	/* disable csc-clock */
	ret = it66121_reg_update_bits(priv, IT66121_SYS_STATUS1,
				      IT66121_SYS_STATUS1_GATE_TXCLK,
				      IT66121_SYS_STATUS1_GATE_TXCLK);
	if (ret < 0)
		return;

	/* disable audio output */
//	it66121_reg_update_bits(priv, IT66121_SW_RST, (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD), (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD));
printk("%s: disabled bridge\n", __func__);
}

static void it66121_bridge_enable(struct drm_bridge *bridge)
{
	struct it66121 *priv = bridge_to_it66121(bridge);

printk("%s: enabling bridge\n", __func__);

	if (priv->need_csc)
		it66121_reg_update_bits(priv, IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_GATE_TXCLK, 0);

	it66121_reg_write(priv, IT66121_AVI_INFOFRM_CTRL, IT66121_INFOFRM_ENABLE_PACKET | IT66121_INFOFRM_REPEAT_PACKET);

	it66121_reg_update_bits(priv, IT66121_SW_RST, IT66121_SW_RST_SOFT_VID, 0);
printk("%s: enabled bridge\n", __func__);
}

static int it66121_bridge_attach(struct drm_bridge *bridge)
{
	struct it66121 *priv = bridge_to_it66121(bridge);
	struct drm_device *drm = bridge->dev;
	int ret;

	drm_connector_helper_add(&priv->connector,
				 &it66121_connector_helper_funcs);

	if (!drm_core_check_feature(drm, DRIVER_ATOMIC)) {
		dev_err(&priv->i2c->dev,
			"sii902x driver is only compatible with DRM devices supporting atomic updates\n");
		return -ENOTSUPP;
	}

	ret = drm_connector_init(drm, &priv->connector,
				 &it66121_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;

	if (priv->i2c->irq > 0)
		priv->connector.polled = DRM_CONNECTOR_POLL_HPD;
	else
		priv->connector.polled = DRM_CONNECTOR_POLL_CONNECT;

	priv->connector.interlace_allowed = 1;

	drm_connector_attach_encoder(&priv->connector, bridge->encoder);

	ret = pm_runtime_get_sync(&priv->i2c->dev);
	if (WARN_ON(ret < 0))
		return ret;

	/* unmask hpd and rx_sense interrupts */
	it66121_reg_update_bits(priv, IT66121_INT_MASK0,
				IT66121_INT_MASK0_RX_SENSE |
				IT66121_INT_MASK0_HPD, 0);

	return 0;
}

static void it66121_bridge_detach(struct drm_bridge *bridge)
{
	struct it66121 *priv = bridge_to_it66121(bridge);

	/* mask hpd and rx_sense interrupts */
	it66121_reg_update_bits(priv, IT66121_INT_MASK0,
				IT66121_INT_MASK0_RX_SENSE |
				IT66121_INT_MASK0_HPD,
				IT66121_INT_MASK0_RX_SENSE |
				IT66121_INT_MASK0_HPD);

	pm_runtime_put(&priv->i2c->dev);
}

static const struct drm_bridge_funcs it66121_bridge_funcs = {
	.attach = it66121_bridge_attach,
	.detach = it66121_bridge_detach,
	.mode_set = it66121_bridge_mode_set,
	.disable = it66121_bridge_disable,
	.enable = it66121_bridge_enable,
};


static void it66121_hpd_work(struct work_struct *work)
{
	struct it66121 *priv = container_of(work, struct it66121, hpd_work);
	enum drm_connector_status status;
	unsigned int val;
	int ret;

printk("%s: start\n", __func__);
	ret = regmap_read(priv->regmap, IT66121_SYS_STATUS0, &val);
	if (ret < 0)
		status = connector_status_disconnected;
	else if (val & IT66121_SYS_STATUS0_HP_DETECT)
		status = connector_status_connected;
	else
		status = connector_status_disconnected;

	/*
	 * The bridge resets its registers on unplug. So when we get a plug
	 * event and we're already supposed to be powered, cycle the bridge to
	 * restore its state.
	 */
/*	if (status == connector_status_connected &&
	    adv7511->connector.status == connector_status_disconnected &&
	    adv7511->powered) {
		regcache_mark_dirty(adv7511->regmap);
		adv7511_power_on(adv7511);
	}*/

	if (priv->connector.status != status) {
printk("%s: send event %d -> %d\n", __func__, priv->connector.status, status);
		priv->connector.status = status;
//		if (status == connector_status_disconnected)
//			cec_phys_addr_invalidate(adv7511->cec_adap);
		drm_kms_helper_hotplug_event(priv->connector.dev);
	}

printk("%s: end\n", __func__);
}

struct it66121_int_clr {
	u8 irq;
	u16 reg;
	u8 bit;
};

static const struct it66121_int_clr it66121_int_stat1_clr[] = {
	{ IT66121_INT_STAT0_RX_SENSE, IT66121_INT_CLR0, IT66121_INT_CLR0_RX_SENSE },
	{ IT66121_INT_STAT0_HPD, IT66121_INT_CLR0, IT66121_INT_CLR0_HPD },
};

static const struct it66121_int_clr it66121_int_stat2_clr[] = {
	{ IT66121_INT_STAT1_VID_UNSTABLE, IT66121_INT_CLR1, IT66121_INT_CLR1_VID_UNSTABLE },
	{ IT66121_INT_STAT1_PKT_ACP, IT66121_INT_CLR0, IT66121_INT_CLR0_PKT_ACP },
	{ IT66121_INT_STAT1_PKT_NULL, IT66121_INT_CLR0, IT66121_INT_CLR0_PKT_NULL },
	{ IT66121_INT_STAT1_PKT_GEN, IT66121_INT_CLR0, IT66121_INT_CLR0_PKT_GEN },
	{ IT66121_INT_STAT1_KSVLIST_CHK, IT66121_INT_CLR0, IT66121_INT_CLR0_KSVLIST_CHK },
	{ IT66121_INT_STAT1_AUTH_DONE, IT66121_INT_CLR0, IT66121_INT_CLR0_AUTH_DONE },
	{ IT66121_INT_STAT1_AUTH_FAIL, IT66121_INT_CLR0, IT66121_INT_CLR0_AUTH_FAIL },
};

static const struct it66121_int_clr it66121_int_stat3_clr[] = {
	{ IT66121_INT_STAT2_AUD_CTS, IT66121_SYS_STATUS0, IT66121_SYS_STATUS0_CLEAR_AUD_CTS },
	{ IT66121_INT_STAT2_VSYNC, IT66121_INT_CLR1, IT66121_INT_CLR1_VSYNC },
	{ IT66121_INT_STAT2_VID_STABLE, IT66121_INT_CLR1, IT66121_INT_CLR1_VID_STABLE },
	{ IT66121_INT_STAT2_PKT_MPG, IT66121_INT_CLR1, IT66121_INT_CLR1_PKT_MPG },
	{ IT66121_INT_STAT2_PKT_AUD, IT66121_INT_CLR1, IT66121_INT_CLR1_PKT_AUD },
	{ IT66121_INT_STAT2_PKT_AVI, IT66121_INT_CLR1, IT66121_INT_CLR1_PKT_AVI },
};

static int it66121_clear_interrupt(struct it66121 *priv, u8 intreg,
				   const struct it66121_int_clr *clr, int clrnum)
{
	int i, ret;

	for (i = 0; i < clrnum; i++) {
		if (intreg & clr[i].irq) {
			ret = it66121_reg_write(priv, clr[i].reg, clr[i].bit);
			if (ret < 0) {
				dev_err(&priv->i2c->dev,
					"failed to clear interrupt: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static irqreturn_t it66121_thread_interrupt(int irq, void *data)
{
	struct it66121 *priv = data;
	int intcore;

	u8 sysstat;
	u8 intdata0;
	u8 intdata1;
	u8 intdata2;
	u8 intdata3;
//	u8 intclr3;

	if (WARN_ON(pm_runtime_get_sync(&priv->i2c->dev) < 0))
		return IRQ_NONE;

	intcore = it66121_reg_read(priv, IT66121_INT_CORE_STAT);
	if (intcore < 0) {
		/* trying to recover by handling all interrupt states */
		dev_warn(&priv->i2c->dev, "failed to read interrupt status\n");
		intcore = IT66121_INT_CORE_STAT_CEC |
			  IT66121_INT_CORE_STAT_EXT |
			  IT66121_INT_CORE_STAT_HDMI;
	}
printk("%s: begin of interrupt, core status 0x%x\n", __func__, intcore);


	intdata0 = it66121_reg_read(priv, IT66121_INT_STAT0);
	intdata1 = it66121_reg_read(priv, IT66121_INT_STAT1);
	intdata2 = it66121_reg_read(priv, IT66121_INT_STAT2);
	intdata3 = it66121_reg_read(priv, IT66121_INT_STAT_EXT);

	it66121_clear_interrupt(priv, intdata0, it66121_int_stat1_clr, ARRAY_SIZE(it66121_int_stat1_clr));
	it66121_clear_interrupt(priv, intdata1, it66121_int_stat2_clr, ARRAY_SIZE(it66121_int_stat2_clr));
	it66121_clear_interrupt(priv, intdata2, it66121_int_stat3_clr, ARRAY_SIZE(it66121_int_stat2_clr));

	/* ext-interrupt is write-1-to-clear */
	if (intdata3)
		it66121_reg_write(priv, IT66121_INT_STAT_EXT, intdata3);

/*	intclr3 = it66121_reg_read(priv, IT66121_SYS_STATUS0);
	intclr3 = intclr3 | IT66121_SYS_STATUS0_CLEAR_AUD_CTS;
	it66121_reg_write(priv, IT66121_INT_CLR0, 0xFF);
	it66121_reg_write(priv, IT66121_INT_CLR1, 0xFF);
	it66121_reg_write(priv, IT66121_SYS_STATUS0, intclr3); // clear interrupt.
*/

	/* mark interrupt as cleared */
	it66121_reg_update_bits(priv, IT66121_SYS_STATUS0, IT66121_SYS_STATUS0_INTACTDONE, IT66121_SYS_STATUS0_INTACTDONE);
	it66121_reg_update_bits(priv, IT66121_SYS_STATUS0, IT66121_SYS_STATUS0_INTACTDONE, 0);

	if (intdata0 & IT66121_INT_STAT0_DDC_FIFO_ERR) {
printk("%s: handling ddc_fifo_err\n", __func__);
		//dev_err(&client->dev, "DDC FIFO Error.\n");
		/*clear ddc fifo*/
		it66121_reg_write(priv, IT66121_DDC_MASTER, IT66121_DDC_MASTER_DDC | IT66121_DDC_MASTER_HOST);
		it66121_reg_write(priv, IT66121_DDC_CMD, IT66121_DDC_CMD_FIFO_CLEAR);
	}

	if (intdata0 & IT66121_INT_STAT0_DDC_BUS_HANG) {
printk("%s: handling ddc_bus_hang\n", __func__);
		//dev_err(&client->dev, "DDC BUS HANG.\n");
		/*abort ddc*/
		it66121_abort_DDC(priv);
	}

	if (intdata0 & IT66121_INT_STAT0_AUDIO_OVERFLOW) {
printk("%s: handling audio overflow\n", __func__);
		//dev_err(&client->dev, "AUDIO FIFO OVERFLOW.\n");
		it66121_reg_update_bits(priv, IT66121_SW_RST, IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD,
					  IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD);
		it66121_reg_update_bits(priv, IT66121_SW_RST, IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD,
					  0);
	}

	if (intdata2 & IT66121_INT_STAT2_VID_STABLE) {
printk("%s: handling vid_stable\n", __func__);
		//dev_info(&client->dev, "it66121 interrupt video enabled\n");
		sysstat = it66121_reg_read(priv, IT66121_SYS_STATUS0);
		if (sysstat & IT66121_SYS_STATUS0_TX_VID_STABLE) {
			/*fire APFE*/
//			it66121_reg_write(priv, IT66121_AFE_DRV_CTRL, 0);
		}
	}

	if ((intdata0 & IT66121_INT_STAT0_HPD) && priv->bridge.dev)
{
printk("%s: handling hotplug\n", __func__);
		schedule_work(&priv->hpd_work);
}

	pm_runtime_put(&priv->i2c->dev);
	return IRQ_HANDLED;
}



static int __maybe_unused it66121_runtime_resume(struct device *dev)
{
	struct it66121 *priv = dev_get_drvdata(dev);
	int ret;

	ret = it66121_reg_update_bits(priv, IT66121_INT_CTRL,
				      IT66121_INT_CTRL_TXCLK_POWERDN, 0);
	if (ret < 0)
		return ret;

	ret = it66121_afe_enable(priv);

	return ret;
}

static int __maybe_unused it66121_runtime_suspend(struct device *dev)
{
	struct it66121 *priv = dev_get_drvdata(dev);
	int ret;

	ret = it66121_afe_disable(priv);
	if (ret < 0)
		return ret;

	ret = it66121_reg_update_bits(priv, IT66121_INT_CTRL,
				      IT66121_INT_CTRL_TXCLK_POWERDN,
				      IT66121_INT_CTRL_TXCLK_POWERDN);

	return ret;
}

static const struct dev_pm_ops it66121_pm_ops = {
	SET_RUNTIME_PM_OPS(it66121_runtime_suspend, it66121_runtime_resume, NULL)
};

static int it66121_init(struct it66121 *priv)
{
	int ret = 0;

	/* ungate the rclk for i2c access */
	ret = it66121_reg_update_bits(priv, IT66121_SYS_STATUS1,
				      IT66121_SYS_STATUS1_GATE_RCLK, 0);
	if (ret < 0)
		return ret;

	/* put RCLK in reset */
	ret = it66121_reg_update_bits(priv, IT66121_SW_RST,
				      IT66121_SW_RST_REF, IT66121_SW_RST_REF);
	if (ret < 0)
		return ret;

	msleep(10);

	ret = it66121_reg_update_bits(priv, IT66121_SW_RST,
				      IT66121_SW_RST_REF, 0);
	if (ret < 0)
		return ret;

	/* configure the host interrupt */
	ret = it66121_reg_update_bits(priv, IT66121_INT_CTRL,
				      IT66121_INT_CTRL_POL_ACT_HIGH |
				      IT66121_INT_CTRL_OPENDRAIN,
				      IT66121_INT_CTRL_OPENDRAIN);
	if (ret < 0)
		return ret;

	ret = it66121_afe_disable(priv);
	if (ret < 0)
		return ret;

	/* put submodules in reset */
	ret = it66121_reg_update_bits(priv, IT66121_SW_RST,
				      IT66121_SW_RST_SOFT_AUD |
				      IT66121_SW_RST_SOFT_VID |
				      IT66121_SW_RST_AUDIO_FIFO |
				      IT66121_SW_RST_HDCP,
				      IT66121_SW_RST_SOFT_AUD |
				      IT66121_SW_RST_SOFT_VID |
				      IT66121_SW_RST_AUDIO_FIFO |
				      IT66121_SW_RST_HDCP);

	/* default speed for ring_ck (now slowdown, speedup) */
	ret = it66121_reg_update_bits(priv,  IT66121_AFE_RING_CTRL,
				      IT66121_AFE_RING_CTRL_CK_SLOW |
				      IT66121_AFE_RING_CTRL_CK_FAST, 0);
	if (ret < 0)
		return ret;

	if (it66121_load_reg_table(priv, it66121_init_table) < 0) {
		dev_err(&priv->i2c->dev, "fail to load init table\n");
		goto err_device;
	}

/*	if (it66121_load_reg_table(priv, it66121_default_video_table) < 0) {
		dev_err(&priv->i2c->dev, "fail to load default video table\n");
		goto err_device;
	}

	if (it66121_load_reg_table(priv, it66121_setHDMI_table) < 0) {
		dev_err(&priv->i2c->dev, "fail to load hdmi table\n");
		goto err_device;
	}

	if (it66121_load_reg_table(priv, it66121_default_AVI_info_table) < 0) {
		dev_err(&priv->i2c->dev, "fail to load default avi table\n");
		goto err_device;
	}

	if (it66121_load_reg_table(priv, it66121_default_audio_info_table) < 0) {
		dev_err(&priv->i2c->dev, "fail to load audio table\n");
		goto err_device;
	}

	if (it66121_load_reg_table(priv, it66121_aud_CHStatus_LPCM_20bit_48Khz) < 0) {
		dev_err(&priv->i2c->dev, "fail to load lpcm table\n");
		goto err_device;
	}

	if (it66121_load_reg_table(priv, it66121_AUD_SPDIF_2ch_24bit) < 0) {
		dev_err(&priv->i2c->dev, "fail to load spdif table\n");
		goto err_device;
	} */

	return ret;

err_device:
	return -ENXIO;;
}

static const struct regmap_config it66121_cec_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0xff,
	.cache_type = REGCACHE_NONE,
};

static void it66121_delete_cec_i2c(void *data)
{
	struct it66121 *priv = data;

	i2c_unregister_device(priv->i2c_cec);
}

static int it66121_cec_regmap_init(struct it66121 *priv)
{
	int ret;

	/* read and configure the cec i2c address */
	priv->cec_addr = IT66121_CEC_SLAVE_ADDRESS_DEFAULT;
	device_property_read_u32(&priv->i2c->dev,
				"ite,cec-address", &priv->cec_addr);

	ret = it66121_reg_write(priv,
				IT66121_CEC_SLAVE_ADDRESS, priv->cec_addr);
	if (ret < 0)
		return ret;

	/* now add the secondary i2c device */
	priv->i2c_cec = i2c_new_secondary_device(priv->i2c, "cec",
						 priv->cec_addr);
	if (!priv->i2c_cec)
		return -EINVAL;

	ret = devm_add_action_or_reset(&priv->i2c->dev,
				       it66121_delete_cec_i2c, priv);
	if (ret < 0)
		return ret;

	i2c_set_clientdata(priv->i2c_cec, priv);

	priv->regmap_cec = devm_regmap_init_i2c(priv->i2c_cec,
					&it66121_cec_regmap_config);
	if (IS_ERR(priv->regmap_cec))
		return PTR_ERR(priv->regmap_cec);

	/* start with the cec clock disabled */
	ret = it66121_reg_update_bits(priv, IT66121_SYS_STATUS1,
					    IT66121_SYS_STATUS1_GATE_CRCLK,
					    IT66121_SYS_STATUS1_GATE_CRCLK);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct regmap_config it66121_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
};

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

static void it66121_uninit_regulators(void *data)
{
	struct it66121 *priv = data;

	regulator_bulk_disable(priv->num_supplies, priv->supplies);
}

static int it66121_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct it66121 *priv;
	u8 chipid[4];
	int i, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->i2c = client;
	i2c_set_clientdata(client, priv);

	priv->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset_gpio)) {
		dev_err(dev, "Failed to retrieve/request reset gpio: %ld\n",
			PTR_ERR(priv->reset_gpio));
		return PTR_ERR(priv->reset_gpio);
	}

	/* supply regulators */
	priv->num_supplies = ARRAY_SIZE(it66121_supply_names);
	priv->supplies = devm_kcalloc(dev, priv->num_supplies,
				     sizeof(*priv->supplies), GFP_KERNEL);
	if (!priv->supplies)
		return -ENOMEM;

	for (i = 0; i < priv->num_supplies; i++)
		priv->supplies[i].supply = it66121_supply_names[i];

	ret = devm_regulator_bulk_get(dev, priv->num_supplies, priv->supplies);
	if (ret)
		return ret;

	ret = regulator_bulk_enable(priv->num_supplies, priv->supplies);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, it66121_uninit_regulators, priv);
	if (ret < 0)
		return ret;

	it66121_reset(priv);

	/* setup regmap and check device id */
	priv->regmap = devm_regmap_init_i2c(client, &it66121_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	ret = regmap_bulk_read(priv->regmap, IT66121_VENDOR_ID0, &chipid, 4);
	if (ret) {
		dev_err(dev, "regmap_read failed %d\n", ret);
		return ret;
	}

	if (chipid[0] != 0x54 || chipid[1] != 0x49 || chipid[2] != 0x12) {
		dev_err(&client->dev, "device not found!\n");
		return ret;
	}
	dev_dbg(dev, "found %x%x:%x%x\n", chipid[0], chipid[1],
					  chipid[2], chipid[3]);

	INIT_WORK(&priv->hpd_work, it66121_hpd_work);

	/* init bank to 0 */
	mutex_init(&priv->bank_mutex);
	it66121_set_bank(priv, 0);

	ret = it66121_init(priv);
	if (ret < 0) {
		dev_err(&priv->i2c->dev, "core init failed\n");
		return ret;
	}

	ret = it66121_audio_init(&client->dev, priv);
	if (ret < 0) {
		dev_err(&priv->i2c->dev, "fail to init hdmi audio\n");
		return ret;
	}

	ret = it66121_cec_regmap_init(priv);
	if (ret < 0) {
		dev_err(&priv->i2c->dev, "fail to init cec device\n");
		return ret;
	}

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						it66121_thread_interrupt,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						dev_name(dev),
						priv);
		if (ret)
			return ret;
	}

	pm_runtime_enable(&client->dev);

	priv->bridge.funcs = &it66121_bridge_funcs;
	priv->bridge.of_node = client->dev.of_node;
	drm_bridge_add(&priv->bridge);

	return 0;
}

static int it66121_remove(struct i2c_client *client)
{
	struct it66121 *priv = i2c_get_clientdata(client);

	drm_bridge_remove(&priv->bridge);
	pm_runtime_disable(&client->dev);
	it66121_audio_exit(priv);

	return 0;
}

static const struct of_device_id it66121_dt_ids[] = {
	{ .compatible = "ite,it66121", },
	{ }
};
MODULE_DEVICE_TABLE(of, it66121_dt_ids);

static struct i2c_device_id it66121_ids[] = {
	{ "it66121", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, it66121_ids);

static struct i2c_driver it66121_driver = {
	.probe = it66121_probe,
	.remove = it66121_remove,
	.driver = {
		.name = "it66121",
		.of_match_table = of_match_ptr(it66121_dt_ids),
		.pm = &it66121_pm_ops,
	},
	.id_table = it66121_ids,
};
module_i2c_driver(it66121_driver);

MODULE_AUTHOR("Baozhu Zuo <zuobaozhu@gmail.com>");
MODULE_DESCRIPTION("IT66121 RGB-HDMI bridge");
MODULE_LICENSE("GPL v2");
