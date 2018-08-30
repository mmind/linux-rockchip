// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Heiko Stuebner <heiko@sntech.de>
 *
 * based on beagleboard it66121 i2c encoder driver
 * Copyright (C) 2017 Baozhu Zuo <zuobaozhu@gmail.com>
 */
#include <linux/gpio/consumer.h>
#include <linux/hdmi.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/string.h>
#include <sound/asoundef.h>
#include <sound/hdmi-codec.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>

#include "it66121.h"
//#define INIT_CLK_HIGH

struct it66121 {
	struct i2c_client *i2c;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
	unsigned int num_supplies;
	struct work_struct hpd_work;

	struct drm_bridge bridge;
	struct drm_connector connector;


	int powerstatus;
	int plug_status;
	u8 AudioChannelEnable;
	struct platform_device *audio_pdev;
};

struct a_reg_entry {
	u8 reg;
	u8 mask;
	u8 val;
};

// #define INIT_CLK_LOW
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
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_GATE_RCLK, 0x00 }, // enabling register programming.
	{ IT66121_AFE_XP_CTRL, IT66121_AFE_XP_CTRL_RESETB, 0x00 },
	{ IT66121_AFE_IP_CTRL, IT66121_AFE_IP_CTRL_RESETB, 0x00 },
	{ 0x01, 0x00, 0x00 }, //idle(100);

	{ IT66121_SW_RST, IT66121_SW_RST_REF, IT66121_SW_RST_REF },  //Software RCLK reset.
	{ IT66121_SW_RST, IT66121_SW_RST_SOFT_AUD | IT66121_SW_RST_SOFT_VID | IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_HDCP, IT66121_SW_RST_SOFT_AUD | IT66121_SW_RST_SOFT_VID | IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_HDCP },
	{ 0x01, 0x00, 0x00 }, //idle(100);

	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, 0x00 }, // bank 0 ;

#ifdef INIT_CLK_LOW
	{ 0x62, 0x90, 0x10 },
	{ 0x64, 0x89, 0x09 },
	{ 0x68, 0x10, 0x10 },
#endif

	{ 0xD1, 0x0E, 0x0C },
	{ 0x65, 0x03, 0x00 },

#ifdef NON_SEQUENTIAL_YCBCR422 // for ITE HDMIRX
	{ 0x71, 0xFC, 0x1C },
#else
	{ 0x71, 0xFC, 0x18 },
#endif

	{ 0x8D, 0xFF, CEC_I2C_SLAVE_ADDR },
	{ IT66121_SYS_STATUS1, 0x08, 0x08 },

	{ 0xF8, 0xFF, 0xC3 },
	{ 0xF8, 0xFF, 0xA5 },
	{ 0x20, 0x80, 0x80 },
	{ 0x37, 0x01, 0x00 },
	{ 0x20, 0x80, 0x00 },
	{ 0xF8, 0xFF, 0xFF },

	{ 0x59, 0xD8, 0x40 | PCLKINV },
	{ 0xE1, 0x20, InvAudCLK },
	{ 0x05, 0xC0, 0x40 },
	{ IT66121_INT_MASK1, 0xFF, ~(IT66121_INT_MASK1_RX_SENSE | IT66121_INT_MASK1_HPD) },
	{ IT66121_INT_MASK2, 0xFF, ~(IT66121_INT_MASK2_KSVLIST_CHK | IT66121_INT_MASK2_AUTH_DONE | IT66121_INT_MASK2_AUTH_FAIL) },
	{ IT66121_INT_MASK3, 0xFF, ~(0x0) },
	{ 0x0C, 0xFF, 0xFF },
	{ 0x0D, 0xFF, 0xFF },
	{ 0x0E, 0x03, 0x03 },

	{ 0x0C, 0xFF, 0x00 },
	{ 0x0D, 0xFF, 0x00 },
	{ 0x0E, 0x02, 0x00 },
	{ 0x09, 0x03, 0x00 }, // Enable HPD and RxSen Interrupt
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
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, 0x00 },
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
	{ 0x04, 0x08, 0x00 },
	{ 0, 0, 0 }
};
static struct a_reg_entry  it66121_setHDMI_table[] = {
	/* Config default HDMI Mode */
	{ 0xC0, 0x01, 0x01 },
	{ 0xC1, 0x03, 0x03 },
	{ 0xC6, 0x03, 0x03 },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_setDVI_table[] = {
	/* Config default DVI Mode */
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, 0x01 },
	{ 0x58, 0xFF, 0x00 },
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, 0x00 },
	{ 0xC0, 0x01, 0x00 },
	{ 0xC1, 0x03, 0x02 },
	{ 0xC6, 0x03, 0x00 },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_default_AVI_info_table[] = {
	/* Config default avi infoframe */
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, 0x01 },
	{ 0x58, 0xFF, 0x10 },
	{ 0x59, 0xFF, 0x08 },
	{ 0x5A, 0xFF, 0x00 },
	{ 0x5B, 0xFF, 0x00 },
	{ 0x5C, 0xFF, 0x00 },
	{ 0x5D, 0xFF, 0x57 },
	{ 0x5E, 0xFF, 0x00 },
	{ 0x5F, 0xFF, 0x00 },
	{ 0x60, 0xFF, 0x00 },
	{ 0x61, 0xFF, 0x00 },
	{ 0x62, 0xFF, 0x00 },
	{ 0x63, 0xFF, 0x00 },
	{ 0x64, 0xFF, 0x00 },
	{ 0x65, 0xFF, 0x00 },
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, 0x00 },
	{ 0xCD, 0x03, 0x03 },
	{ 0, 0, 0 }
};
static struct a_reg_entry  it66121_default_audio_info_table[] = {
	/* Config default audio infoframe */
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, 0x01 },
	{ 0x68, 0xFF, 0x00 },
	{ 0x69, 0xFF, 0x00 },
	{ 0x6A, 0xFF, 0x00 },
	{ 0x6B, 0xFF, 0x00 },
	{ 0x6C, 0xFF, 0x00 },
	{ 0x6D, 0xFF, 0x71 },
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, 0x00 },
	{ 0xCE, 0x03, 0x03 },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_aud_CHStatus_LPCM_20bit_48Khz[] = {
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, 0x01 },
	{ 0x33, 0xFF, 0x00 },
	{ 0x34, 0xFF, 0x18 },
	{ 0x35, 0xFF, 0x00 },
	{ 0x91, 0xFF, 0x00 },
	{ 0x92, 0xFF, 0x00 },
	{ 0x93, 0xFF, 0x01 },
	{ 0x94, 0xFF, 0x00 },
	{ 0x98, 0xFF, 0x02 },
	{ 0x99, 0xFF, 0xDA },
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, 0x00 },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_AUD_SPDIF_2ch_24bit[] = {
	{ IT66121_SYS_STATUS1, 0x10 | IT66121_SYS_STATUS1_BANK_MASK, 0x00 },
	{ 0x04, 0x14, 0x04 },
	{ 0xE0, 0xFF, 0xD1 },
	{ 0xE1, 0xFF, 0x01 },
	{ 0xE2, 0xFF, 0xE4 },
	{ 0xE3, 0xFF, 0x10 },
	{ 0xE4, 0xFF, 0x00 },
	{ 0xE5, 0xFF, 0x00 },
	{ 0x04, 0x14, 0x00 },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_AUD_I2S_2ch_24bit[] = {
	{ IT66121_SYS_STATUS1, 0x10 | IT66121_SYS_STATUS1_BANK_MASK, 0x00 },
	{ 0x04, 0x14, 0x04 },
	{ 0xE0, 0xFF, 0xC1 },
	{ 0xE1, 0xFF, 0x01 },
	{ 0xE2, 0xFF, 0xE4 },
	{ 0xE3, 0xFF, 0x00 },
	{ 0xE4, 0xFF, 0x00 },
	{ 0xE5, 0xFF, 0x00 },
	{ 0x04, 0x14, 0x00 },
	{ 0, 0, 0 }
};

static struct a_reg_entry  it66121_default_audio_table[] = {
	/* Config default audio output format. */
	{ IT66121_SYS_STATUS1, 0x20 | IT66121_SYS_STATUS1_BANK_MASK, 0x00 },
	{ 0x04, 0x14, 0x04 },
	{ 0xE0, 0xFF, 0xC1 },
	{ 0xE1, 0xFF, 0x01 },
	{ 0xE2, 0xFF, 0xE4 },
	{ 0xE3, 0xFF, 0x00 },
	{ 0xE4, 0xFF, 0x00 },
	{ 0xE5, 0xFF, 0x00 },
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, 0x01 },
	{ 0x33, 0xFF, 0x00 },
	{ 0x34, 0xFF, 0x18 },
	{ 0x35, 0xFF, 0x00 },
	{ 0x91, 0xFF, 0x00 },
	{ 0x92, 0xFF, 0x00 },
	{ 0x93, 0xFF, 0x01 },
	{ 0x94, 0xFF, 0x00 },
	{ 0x98, 0xFF, 0x02 },
	{ 0x99, 0xFF, 0xDB },
	{ IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, 0x00 },
	{ 0x04, 0x14, 0x00 },
	{ 0, 0, 0 }
};


static int it66121_power_down(struct it66121 *priv)
{
	/* enable rclk */
	regmap_update_bits(priv->regmap, IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_GATE_RCLK, 0);

	// PLL Reset
	regmap_update_bits(priv->regmap, IT66121_AFE_DRV_CTRL, IT66121_AFE_DRV_CTRL_RST, IT66121_AFE_DRV_CTRL_RST);
	regmap_update_bits(priv->regmap, IT66121_AFE_XP_CTRL, IT66121_AFE_XP_CTRL_RESETB, 0);
	regmap_update_bits(priv->regmap, IT66121_AFE_IP_CTRL, IT66121_AFE_IP_CTRL_RESETB, 0);

	msleep(100);

	/* PLL power down */
	regmap_update_bits(priv->regmap, IT66121_AFE_DRV_CTRL, IT66121_AFE_DRV_CTRL_PWD, IT66121_AFE_DRV_CTRL_PWD);
	regmap_update_bits(priv->regmap, IT66121_AFE_XP_CTRL, IT66121_AFE_XP_CTRL_PWDPLL | IT66121_AFE_XP_CTRL_PWDI, IT66121_AFE_XP_CTRL_PWDPLL | IT66121_AFE_XP_CTRL_PWDI);
	regmap_update_bits(priv->regmap, IT66121_AFE_IP_CTRL, IT66121_AFE_IP_CTRL_PWDPLL, IT66121_AFE_IP_CTRL_PWDPLL);

	/* HDMITX power down */
	regmap_update_bits(priv->regmap, IT66121_INT_CTRL, IT66121_INT_CTRL_TXCLK_POWERDN, IT66121_INT_CTRL_TXCLK_POWERDN);
//	regmap_update_bits(priv->regmap, IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_GATE_RCLK | IT66121_SYS_STATUS1_GATE_IACLK | IT66121_SYS_STATUS1_GATE_TXCLK | IT66121_SYS_STATUS1_GATE_CRCLK, IT66121_SYS_STATUS1_GATE_RCLK | IT66121_SYS_STATUS1_GATE_IACLK | IT66121_SYS_STATUS1_GATE_TXCLK | IT66121_SYS_STATUS1_GATE_CRCLK);

	return 0;
}

static int it66121_power_up(struct it66121 *priv)
{
	/* enable rclk */
	regmap_update_bits(priv->regmap, IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_GATE_RCLK | IT66121_SYS_STATUS1_GATE_IACLK | IT66121_SYS_STATUS1_GATE_TXCLK | IT66121_SYS_STATUS1_GATE_CRCLK, IT66121_SYS_STATUS1_GATE_IACLK | IT66121_SYS_STATUS1_GATE_TXCLK | IT66121_SYS_STATUS1_GATE_CRCLK);

	regmap_update_bits(priv->regmap, IT66121_INT_CTRL, IT66121_INT_CTRL_TXCLK_POWERDN, 0);

	// PLL PwrOn
	regmap_update_bits(priv->regmap, IT66121_AFE_DRV_CTRL, IT66121_AFE_DRV_CTRL_PWD, 0);
	regmap_update_bits(priv->regmap, IT66121_AFE_XP_CTRL, IT66121_AFE_XP_CTRL_PWDPLL | IT66121_AFE_XP_CTRL_PWDI, 0);
	regmap_update_bits(priv->regmap, IT66121_AFE_IP_CTRL, IT66121_AFE_IP_CTRL_PWDPLL, 0);

	msleep(100);

	// PLL Reset OFF
	regmap_update_bits(priv->regmap, IT66121_AFE_DRV_CTRL, IT66121_AFE_DRV_CTRL_RST, 0);
	regmap_update_bits(priv->regmap, IT66121_AFE_XP_CTRL, IT66121_AFE_XP_CTRL_RESETB, IT66121_AFE_XP_CTRL_RESETB);
	regmap_update_bits(priv->regmap, IT66121_AFE_IP_CTRL, IT66121_AFE_IP_CTRL_RESETB, IT66121_AFE_IP_CTRL_RESETB);

	regmap_update_bits(priv->regmap, IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_GATE_RCLK | IT66121_SYS_STATUS1_GATE_IACLK | IT66121_SYS_STATUS1_GATE_TXCLK | IT66121_SYS_STATUS1_GATE_CRCLK, IT66121_SYS_STATUS1_GATE_CRCLK);
}

static u8 reg_read(struct it66121 *priv, u8 reg)
{
	unsigned int val;

	regmap_read(priv->regmap, reg, &val);
printk("it66121: read 0x%x from 0x%x\n", val, reg);

	return val;
}

static int reg_write(struct it66121 *priv, u8 reg, u8 val)
{
	int ret;

	ret = regmap_write(priv->regmap, reg, val);
	if (ret < 0)
		return ret;
printk("it66121: wrote 0x%x to 0x%x\n", val, reg);

	return 0;
}

static int write_a_entry(struct it66121 *priv, u8 reg, u8 mask, u8 val)
{
	regmap_update_bits(priv->regmap, reg, mask, val);
printk("it66121: update 0x%x/0x%x in 0x%x\n", val, mask, reg);

	return 0;
}

static inline void it66121_switch_bank(struct it66121 *priv, u8 bank)
{
	write_a_entry(priv, IT66121_SYS_STATUS1, IT66121_SYS_STATUS1_BANK_MASK, bank);
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
			ret = reg_write(priv, table[i].reg, table[i].val);
		} else {
			ret = write_a_entry(priv, table[i].reg, table[i].mask, table[i].val);
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

	isconnect = reg_read(priv, IT66121_SYS_STATUS0);
	isconnect &= IT66121_SYS_STATUS0_HP_DETECT;
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
	SWReset = reg_read(priv, IT66121_SW_RST);
	CPDesire = reg_read(priv, IT66121_HDCP);
	DDCMaster = reg_read(priv, IT66121_DDC_MASTER);

	reg_write(priv, IT66121_HDCP, CPDesire & (IT66121_HDCP_DESIRED));
	reg_write(priv, IT66121_SW_RST, SWReset | IT66121_SW_RST_HDCP);
	reg_write(priv, IT66121_DDC_MASTER, IT66121_DDC_MASTER_DDC | IT66121_DDC_MASTER_HOST);

	// 2009/01/15 modified by Jau-Chih.Tseng@ite.com.tw
	// do abort DDC twice.
	for (i = 0; i < 2; i++) {
		reg_write(priv, IT66121_DDC_CMD, IT66121_DDC_CMD_DDC_ABORT);

		for (timeout = 0; timeout < 200; timeout++) {
			uc = reg_read(priv, IT66121_DDC_STATUS);
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

	if (!buf) return -1;

	if (reg_read(priv, IT66121_INT_STAT1) & IT66121_INT_STAT1_DDC_BUS_HANG) {
		dev_err(&priv->i2c->dev, "Sorry, ddc bus is hang\n");
		it66121_abort_DDC(priv);
	}

	/*clear the DDC FIFO*/
	reg_write(priv, IT66121_DDC_MASTER, IT66121_DDC_MASTER_DDC | IT66121_DDC_MASTER_HOST);
	reg_write(priv, IT66121_DDC_CMD, IT66121_DDC_CMD_FIFO_CLEAR);

	bCurrOffset = (blk % 2) / length;
	bSegment  = blk / length;
	it66121_switch_bank(priv, 0);

	while (length > 0) {
		ReqCount = (length > DDC_FIFO_MAXREQ) ? DDC_FIFO_MAXREQ : length;

		reg_write(priv, IT66121_DDC_MASTER, IT66121_DDC_MASTER_DDC | IT66121_DDC_MASTER_HOST);
		reg_write(priv, IT66121_DDC_CMD, IT66121_DDC_CMD_FIFO_CLEAR);

		for (TimeOut = 0; TimeOut < 200; TimeOut++) {
			ucdata = reg_read(priv, IT66121_DDC_STATUS);

			if (ucdata & IT66121_DDC_STATUS_DONE)
				break;

			if ((ucdata & IT66121_DDC_STATUS_ERROR) || (reg_read(priv, IT66121_INT_STAT1) & IT66121_INT_STAT1_DDC_BUS_HANG)) {
				dev_err(&priv->i2c->dev, "it66121_read_edid_block(): DDC_STATUS = %02X,fail.\n", (int)ucdata);
				/*clear the DDC FIFO*/
				reg_write(priv, IT66121_DDC_MASTER, IT66121_DDC_MASTER_DDC | IT66121_DDC_MASTER_HOST);
				reg_write(priv, IT66121_DDC_CMD, IT66121_DDC_CMD_FIFO_CLEAR);
				return -1;
			}
		}
		reg_write(priv, IT66121_DDC_MASTER, IT66121_DDC_MASTER_DDC | IT66121_DDC_MASTER_HOST);
		reg_write(priv, IT66121_DDC_HEADER, DDC_EDID_ADDRESS); // for EDID ucdata get 0x11
		reg_write(priv, IT66121_DDC_REQOFFSET, bCurrOffset); //0x12
		reg_write(priv, IT66121_DDC_REQCOUNT, (u8)ReqCount); //0x13
		reg_write(priv, IT66121_DDC_SEGMENT, bSegment); //0x14
		reg_write(priv, IT66121_DDC_CMD, IT66121_DDC_CMD_EDID_READ); //0x15

		bCurrOffset += ReqCount;
		length -= ReqCount;

		for (TimeOut = 250; TimeOut > 0; TimeOut--) {
			mdelay(1);
			ucdata = reg_read(priv, IT66121_DDC_STATUS);
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
			*(pBuff++) = reg_read(priv, IT66121_DDC_READ_FIFO);
			ReqCount--;
		} while (ReqCount > 0);

	}

	return 0;
}

static int it66121_connector_get_modes(struct drm_connector *connector)
{
	struct it66121 *priv = connector_to_it66121(connector);
	struct edid *edid;
	int num = 0;

	edid = drm_do_get_edid(connector, it66121_read_edid_block, priv);
	if (!edid) {
		dev_warn(&priv->i2c->dev, "failed to read EDID\n");
		return 0;
	}

	drm_connector_update_edid_property(connector, edid);
	if (edid) {
		num = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	printk("it66121_connector_get_modes->color_formats: %x", connector->display_info.color_formats);
	return num;
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

static void it66121_bridge_disable(struct drm_bridge *bridge)
{
	struct it66121 *priv = bridge_to_it66121(bridge);
	u8 udata;

printk("%s: disabling bridge\n", __func__);
	/* disable video output */
	udata = reg_read(priv, IT66121_SW_RST) | IT66121_SW_RST_SOFT_VID;
	reg_write(priv, IT66121_SW_RST, udata);
	reg_write(priv, IT66121_AFE_DRV_CTRL, IT66121_AFE_DRV_CTRL_RST | IT66121_AFE_DRV_CTRL_PWD);
	write_a_entry(priv, 0x62, 0x90, 0x00);
	write_a_entry(priv, 0x64, 0x89, 0x00);

	/* disable audio output */
	write_a_entry(priv, IT66121_SW_RST, (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD), (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD));
	write_a_entry(priv, IT66121_SYS_STATUS1, 0x10, 0x10);

printk("%s: powering down\n", __func__);
	it66121_power_down(priv);
printk("%s: disabled bridge\n", __func__);
}

static void it66121_set_CSC_scale(struct it66121 *priv,
				  u8 input_color_mode)
{
	u8 csc = 0;
	u8 filter = 0;
	u8 udata = 0;
	int i;

	switch (input_color_mode & F_MODE_CLRMOD_MASK) {
	case F_MODE_YUV444:
		switch (OUTPUT_COLOR_MODE & F_MODE_CLRMOD_MASK) {
		case F_MODE_YUV444:
			csc = B_HDMITX_CSC_BYPASS;
			break;
		case F_MODE_YUV422:
			csc = B_HDMITX_CSC_BYPASS;
			if (input_color_mode & F_VIDMODE_EN_UDFILT) // YUV444 to YUV422 need up/down filter for processing.
				filter |= B_TX_EN_UDFILTER;
			break;
		case F_MODE_RGB444:
			csc = B_HDMITX_CSC_YUV2RGB;
			if (input_color_mode & F_VIDMODE_EN_DITHER) // YUV444 to RGB24 need dither
				filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO;
			break;
		}
		break;
	case F_MODE_YUV422:
		switch (OUTPUT_COLOR_MODE & F_MODE_CLRMOD_MASK) {
		case F_MODE_YUV444:
			csc = B_HDMITX_CSC_BYPASS;
			if (input_color_mode & F_VIDMODE_EN_UDFILT) // YUV422 to YUV444 need up filter
				filter |= B_TX_EN_UDFILTER;
			if (input_color_mode & F_VIDMODE_EN_DITHER) // YUV422 to YUV444 need dither
				filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO;
			break;
		case F_MODE_YUV422:
			csc = B_HDMITX_CSC_BYPASS;
			break;
		case F_MODE_RGB444:
			csc = B_HDMITX_CSC_YUV2RGB;
			if (input_color_mode & F_VIDMODE_EN_UDFILT) // YUV422 to RGB24 need up/dn filter.
				filter |= B_TX_EN_UDFILTER;
			if (input_color_mode & F_VIDMODE_EN_DITHER) // YUV422 to RGB24 need dither
				filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO;
			break;
		}
		break;
	case F_MODE_RGB444:
		switch (OUTPUT_COLOR_MODE & F_MODE_CLRMOD_MASK) {
		case F_MODE_YUV444:
			csc = B_HDMITX_CSC_RGB2YUV;
			if (INPUT_COLOR_MODE & F_VIDMODE_EN_DITHER) // RGB24 to YUV444 need dither
				filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO;
			break;
		case F_MODE_YUV422:
			if (input_color_mode & F_VIDMODE_EN_UDFILT) // RGB24 to YUV422 need down filter.
				filter |= B_TX_EN_UDFILTER;
			if (input_color_mode & F_VIDMODE_EN_DITHER) // RGB24 to YUV422 need dither
				filter |= B_TX_EN_DITHER | B_TX_DNFREE_GO;
			csc = B_HDMITX_CSC_RGB2YUV;
			break;
		case F_MODE_RGB444:
			csc = B_HDMITX_CSC_BYPASS;
			break;
		}
		break;
	}

	if (csc == B_HDMITX_CSC_RGB2YUV) {
		switch (input_color_mode & (F_VIDMODE_ITU709 | F_VIDMODE_16_235)) {
		case F_VIDMODE_ITU709 | F_VIDMODE_16_235:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				reg_write(priv, IT66121_CSC_YOFF + i, bCSCMtx_RGB2YUV_ITU709_16_235[i]);
			break;
		case F_VIDMODE_ITU709 | F_VIDMODE_0_255:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				reg_write(priv, IT66121_CSC_YOFF + i, bCSCMtx_RGB2YUV_ITU709_0_255[i]);
			break;
		case F_VIDMODE_ITU601 | F_VIDMODE_16_235:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				reg_write(priv, IT66121_CSC_YOFF + i, bCSCMtx_RGB2YUV_ITU601_16_235[i]);
			break;
		case F_VIDMODE_ITU601 | F_VIDMODE_0_255:
		default:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				reg_write(priv, IT66121_CSC_YOFF + i, bCSCMtx_RGB2YUV_ITU601_0_255[i]);
			break;
		}
	}

	if (csc == B_HDMITX_CSC_YUV2RGB) {
		switch (input_color_mode & (F_VIDMODE_ITU709 | F_VIDMODE_16_235)) {
		case F_VIDMODE_ITU709 | F_VIDMODE_16_235:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				reg_write(priv, IT66121_CSC_YOFF + i, bCSCMtx_YUV2RGB_ITU709_16_235[i]);
			break;
		case F_VIDMODE_ITU709 | F_VIDMODE_0_255:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				reg_write(priv, IT66121_CSC_YOFF + i, bCSCMtx_YUV2RGB_ITU709_0_255[i]);
			break;
		case F_VIDMODE_ITU601 | F_VIDMODE_16_235:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				reg_write(priv, IT66121_CSC_YOFF + i, bCSCMtx_YUV2RGB_ITU601_16_235[i]);
			break;
		case F_VIDMODE_ITU601 | F_VIDMODE_0_255:
		default:
			for (i = 0; i < SIZEOF_CSCMTX; i++)
				reg_write(priv, IT66121_CSC_YOFF + i, bCSCMtx_YUV2RGB_ITU601_0_255[i]);
			break;
		}
	}

	if (csc == B_HDMITX_CSC_BYPASS)
		write_a_entry(priv, 0xF, 0x10, 0x10);
	else
		write_a_entry(priv, 0xF, 0x10, 0x00);

	udata = reg_read(priv, IT66121_CSC_CTRL) & ~(M_TX_CSC_SEL | B_TX_DNFREE_GO | B_TX_EN_DITHER | B_TX_EN_UDFILTER);
	udata |= filter | csc;

	reg_write(priv, IT66121_CSC_CTRL, udata);
}

static void it66121_setup_AFE(struct it66121 *priv, u8 level)
{
	reg_write(priv, IT66121_AFE_DRV_CTRL, IT66121_AFE_DRV_CTRL_RST); /* 0x10 */
	switch (level) {
	case 1:
		write_a_entry(priv, 0x62, 0x90, 0x80);
		write_a_entry(priv, 0x64, 0x89, 0x80);
		write_a_entry(priv, 0x68, 0x10, 0x80);
		break;
	default:
		write_a_entry(priv, 0x62, 0x90, 0x10);
		write_a_entry(priv, 0x64, 0x89, 0x09);
		write_a_entry(priv, 0x68, 0x10, 0x10);
		break;
	}

	write_a_entry(priv, IT66121_SW_RST, IT66121_SW_RST_REF | IT66121_SW_RST_SOFT_VID, 0);
	reg_write(priv, IT66121_AFE_DRV_CTRL, 0);
	mdelay(1);
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
	int name_len = 0;
	u8 is_high_clk = 0;
	u8 pixelrep = 0;
	u8 input_color_mode = F_MODE_RGB444;
	u8 aspec = 0;
	u8 Colorimetry = 0;
	u8 udata;
	u8 vic;

	vic = drm_match_cea_mode(mode);
	name_len = strlen(mode->name);
	if (mode->name[name_len - 1] == 'i')
		pixelrep = 1;

	if ((pixelrep + 1) * (mode->clock * 1000) > 80000000L)
		is_high_clk = 1;

	if (mode->hdisplay * 9 == mode->vdisplay * 16) {
		aspec = HDMI_16x9;
		Colorimetry = HDMI_ITU709;
	}

	if (mode->hdisplay * 3 == mode->vdisplay * 4) {
		aspec = HDMI_4x3;
		Colorimetry = HDMI_ITU601;
	}

	if (Colorimetry == HDMI_ITU709) {
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

	is_high_clk ? reg_write(priv, IT66121_PLL_CTRL, 0x30 /*0xff*/) :
		reg_write(priv, IT66121_PLL_CTRL, 0x00);

	reg_write(priv, IT66121_SW_RST, IT66121_SW_RST_SOFT_VID |
			  IT66121_SW_RST_AUDIO_FIFO |
			  IT66121_SW_RST_SOFT_AUD |
			  IT66121_SW_RST_HDCP);

	// 2009/12/09 added by jau-chih.tseng@ite.com.tw
	it66121_switch_bank(priv, 1);
	reg_write(priv, IT66121_AVIINFO_DB1, 0x00);
	it66121_switch_bank(priv, 0);
	//~jau-chih.tseng@ite.com.tw

	/*Set regC1[0] = '1' for AVMUTE the output, only support hdmi mode now*/
	//write_a_entry(priv, IT66121_GCP, B_TX_SETAVMUTE,  B_TX_SETAVMUTE);
	write_a_entry(priv, IT66121_GCP, B_TX_SETAVMUTE,  0);
	reg_write(priv, IT66121_PKT_GENERAL_CTRL, B_TX_ENABLE_PKT | B_TX_REPEAT_PKT);

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
	udata = reg_read(priv, IT66121_INPUT_MODE);
	udata &= ~(M_TX_INCOLMOD | B_TX_2X656CLK | B_TX_SYNCEMB | B_TX_INDDR | B_TX_PCLKDIV2);
	udata |= 0x01; //input clock delay 1 for 1080P DDR
	udata |= input_color_mode & F_MODE_CLRMOD_MASK; //define in it66121.h, F_MODE_RGB444
	udata |= INPUT_SIGNAL_TYPE; //define in it66121.h,  24 bit sync seperate
	reg_write(priv, IT66121_INPUT_MODE, udata);

	/*
	 * Set color space converting by the input color space and output color space.
	*/
	it66121_set_CSC_scale(priv, input_color_mode);
	/*hdmi mode*/
	reg_write(priv, IT66121_HDMI_MODE, B_TX_HDMI_MODE);
	/*dvi mode*/
	//reg_write(priv,IT66121_HDMI_MODE, B_TX_DVI_MODE);
#ifdef INVERT_VID_LATCHEDGE
	udata = reg_read(priv, IT66121_CLK_CTRL1);
	udata |= IT66121_CLK_CTRL1_VDO_LATCH_EDGE;
	reg_write(priv, IT66121_CLK_CTRL1, udata);
#endif

	it66121_setup_AFE(priv, is_high_clk); // pass if High Freq request
	reg_write(priv, IT66121_SW_RST, IT66121_SW_RST_AUDIO_FIFO |
			  IT66121_SW_RST_SOFT_AUD |
			  IT66121_SW_RST_HDCP);

	/*fire APFE*/
	it66121_switch_bank(priv, 0);
	reg_write(priv, IT66121_AFE_DRV_CTRL, 0);

//	it66121_config_avi_info_frame(priv, aspec, Colorimetry, pixelrep, vic);

	printk("color_formats: %x", priv->connector.display_info.color_formats);
}


static void it66121_bridge_mode_set(struct drm_bridge *bridge,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adj)
{
	struct it66121 *priv = bridge_to_it66121(bridge);
	struct hdmi_avi_infoframe frame;
	u8 buf[HDMI_INFOFRAME_SIZE(AVI)];
	int ret;


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

	it66121_switch_bank(priv, 1);
	regmap_bulk_write(priv->regmap, IT66121_AVIINFO_DB1,
			  buf + HDMI_INFOFRAME_HEADER_SIZE, 5);
	regmap_bulk_write(priv->regmap, IT66121_AVIINFO_DB6,
			  buf + HDMI_INFOFRAME_HEADER_SIZE + 5,
			  HDMI_AVI_INFOFRAME_SIZE - 5);
	regmap_write(priv->regmap, IT66121_AVIINFO_SUM, buf[3]);
	it66121_switch_bank(priv, 0);

	regmap_write(priv->regmap, IT66121_AVI_INFOFRM_CTRL, B_TX_ENABLE_PKT | B_TX_REPEAT_PKT);

	it66121_enable_video_output(priv, adj);
}


static void it66121_bridge_enable(struct drm_bridge *bridge)
{
	struct it66121 *priv = bridge_to_it66121(bridge);

	it66121_power_up(priv);
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

	return 0;
}

static const struct drm_bridge_funcs it66121_bridge_funcs = {
	.attach = it66121_bridge_attach,
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
		priv->connector.status = status;
//		if (status == connector_status_disconnected)
//			cec_phys_addr_invalidate(adv7511->cec_adap);
		drm_kms_helper_hotplug_event(priv->connector.dev);
	}
}

static irqreturn_t it66121_thread_interrupt(int irq, void *data)
{
	struct it66121 *priv = data;
	u8 sysstat;
	u8 intdata1;
	u8 intdata2;
	u8 intdata3;
	u8 udata;
	char intclr3, intdata4;

printk("%s: begin of interrupt\n", __func__);
	sysstat = reg_read(priv, IT66121_SYS_STATUS0);
	intdata1 = reg_read(priv, IT66121_INT_STAT1);
	intdata2 = reg_read(priv, IT66121_INT_STAT2);
	intdata3 = reg_read(priv, IT66121_INT_STAT3);
	intdata4 = reg_read(priv, 0xee);
	intclr3 = reg_read(priv, IT66121_SYS_STATUS0);
	intclr3 = intclr3 | IT66121_SYS_STATUS0_CLEAR_AUD_CTS | IT66121_SYS_STATUS0_INTACTDONE;
	if (intdata4)
		reg_write(priv, 0xEE, intdata4); // clear ext interrupt ;

	reg_write(priv, IT66121_INT_CLR0, 0xFF);
	reg_write(priv, IT66121_INT_CLR1, 0xFF);
	reg_write(priv, IT66121_SYS_STATUS0, intclr3); // clear interrupt.
	intclr3 &= ~(IT66121_SYS_STATUS0_INTACTDONE);
	reg_write(priv, IT66121_SYS_STATUS0, intclr3); // INTACTDONE reset to zero.

	if (intdata1 & IT66121_INT_STAT1_DDC_FIFO_ERR) {
printk("%s: handling ddc_fifo_err\n", __func__);
		//dev_err(&client->dev, "DDC FIFO Error.\n");
		/*clear ddc fifo*/
		reg_write(priv, IT66121_DDC_MASTER, IT66121_DDC_MASTER_DDC | IT66121_DDC_MASTER_HOST);
		reg_write(priv, IT66121_DDC_CMD, IT66121_DDC_CMD_FIFO_CLEAR);
	}

	if (intdata1 & IT66121_INT_STAT1_DDC_BUS_HANG) {
printk("%s: handling ddc_bus_hang\n", __func__);
		//dev_err(&client->dev, "DDC BUS HANG.\n");
		/*abort ddc*/
		it66121_abort_DDC(priv);
	}

	if (intdata1 & IT66121_INT_STAT1_AUDIO_OVERFLOW) {
printk("%s: handling audio overflow\n", __func__);
		//dev_err(&client->dev, "AUDIO FIFO OVERFLOW.\n");
		write_a_entry(priv, IT66121_SW_RST, (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD),
					  (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD));
		udata = reg_read(priv, IT66121_SW_RST);
		reg_write(priv, IT66121_SW_RST, udata & (~(IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD)));
	}

	if (intdata3 & IT66121_INT_STAT3_VID_STABLE) {
printk("%s: handling vid_stable\n", __func__);
		//dev_info(&client->dev, "it66121 interrupt video enabled\n");
		sysstat = reg_read(priv, IT66121_SYS_STATUS0);
		if (sysstat & IT66121_SYS_STATUS0_TX_VID_STABLE) {
			/*fire APFE*/
			it66121_switch_bank(priv, 0);
			reg_write(priv, IT66121_AFE_DRV_CTRL, 0);
		}
	}

	if ((intdata1 & IT66121_INT_STAT1_HPD) && priv->bridge.dev)
{
printk("%s: handling hotplug\n", __func__);
		schedule_work(&priv->hpd_work);
}

printk("%s: end of interrupt status:\n", __func__);
	sysstat = reg_read(priv, IT66121_SYS_STATUS0);
	intdata1 = reg_read(priv, IT66121_INT_STAT1);
	intdata2 = reg_read(priv, IT66121_INT_STAT2);
	intdata3 = reg_read(priv, IT66121_INT_STAT3);
	intdata4 = reg_read(priv, 0xee);

	return IRQ_HANDLED;
}

static void it66121_aud_config_aai(struct it66121 *priv)
{
	u8 aud_db[AUDIO_INFOFRAME_LEN];
	unsigned int checksum = 0;
	u8 i;

//FIXME check and use hdmi_audio_infoframe_pack
	aud_db[0] = 1;

	for (i = 1; i < AUDIO_INFOFRAME_LEN; i++) {
		aud_db[i] = 0;
	}

	it66121_switch_bank(priv, 1);
	checksum = 0x100 - (AUDIO_INFOFRAME_VER + AUDIO_INFOFRAME_TYPE + AUDIO_INFOFRAME_LEN);
	reg_write(priv, IT66121_PKT_AUDINFO_CC, aud_db[0]);
	checksum -= reg_read(priv, IT66121_PKT_AUDINFO_CC);
	checksum &= 0xFF;

	reg_write(priv, IT66121_PKT_AUDINFO_SF, aud_db[1]);
	checksum -= reg_read(priv, IT66121_PKT_AUDINFO_SF);
	checksum &= 0xFF;

	reg_write(priv, IT66121_PKT_AUDINFO_CA, aud_db[3]);
	checksum -= reg_read(priv, IT66121_PKT_AUDINFO_CA);
	checksum &= 0xFF;

	reg_write(priv, IT66121_PKT_AUDINFO_DM_LSV, aud_db[4]);
	checksum -= reg_read(priv, IT66121_PKT_AUDINFO_DM_LSV);
	checksum &= 0xFF;

	reg_write(priv, IT66121_PKT_AUDINFO_SUM, checksum);

	it66121_switch_bank(priv, 0);
	reg_write(priv, IT66121_AUD_INFOFRM_CTRL, B_TX_ENABLE_PKT | B_TX_REPEAT_PKT);
}

static void it66121_aud_set_fs(struct it66121 *priv, u8 fs)
{
	u32 n;
	u32  LastCTS = 0;
	u8 HBR_mode;
	u8 udata;

	if (B_TX_HBR & reg_read(priv, IT66121_AUD_HDAUDIO))
		HBR_mode = 1;
	else
		HBR_mode = 0;

	printk("HBR_mode:%d\n", HBR_mode);
	switch (fs) {
	case AUDFS_32KHz:
		n = 4096; break;
	case AUDFS_44p1KHz:
		n = 6272; break;
	case AUDFS_48KHz:
		n = 6144; break;
	case AUDFS_88p2KHz:
		n = 12544; break;
	case AUDFS_96KHz:
		n = 12288; break;
	case AUDFS_176p4KHz:
		n = 25088; break;
	case AUDFS_192KHz:
		n = 24576; break;
	case AUDFS_768KHz:
		n = 24576; break;
	default:
		n = 6144;
	}

	// tr_printf((" n = %ld\n",n));
	it66121_switch_bank(priv, 1);
	reg_write(priv, REGPktAudN0, (u8)((n)&0xFF));
	reg_write(priv, REGPktAudN1, (u8)((n >> 8) & 0xFF));
	reg_write(priv, REGPktAudN2, (u8)((n >> 16) & 0xF));

	reg_write(priv, REGPktAudCTS0, (u8)((LastCTS)&0xFF));
	reg_write(priv, REGPktAudCTS1, (u8)((LastCTS >> 8) & 0xFF));
	reg_write(priv, REGPktAudCTS2, (u8)((LastCTS >> 16) & 0xF));
	it66121_switch_bank(priv, 0);

	reg_write(priv, 0xF8, 0xC3);
	reg_write(priv, 0xF8, 0xA5);

	udata =  reg_read(priv, IT66121_PKT_SINGLE_CTRL);
	udata &= ~B_TX_SW_CTS;
	reg_write(priv, IT66121_PKT_SINGLE_CTRL, udata);

	reg_write(priv, 0xF8, 0xFF);

	if (0 == HBR_mode) { //LPCM
		it66121_switch_bank(priv, 1);
		fs = AUDFS_768KHz;
		reg_write(priv, IT66121_AUDCHST_CA_FS, 0x00 | fs);
		fs = ~fs; // OFS is the one's complement of FS
		udata = (0x0f & reg_read(priv, IT66121_AUDCHST_OFS_WL));
		reg_write(priv, IT66121_AUDCHST_OFS_WL, (fs << 4) | udata);
		it66121_switch_bank(priv, 0);
	}
}

static void it66121_set_ChStat(struct it66121 *priv, u8 ucIEC60958ChStat[])
{
	u8 udata;

	it66121_switch_bank(priv, 1);
	udata = (ucIEC60958ChStat[0] << 1) & 0x7C;
	reg_write(priv, IT66121_AUDCHST_MODE, udata);
	reg_write(priv, IT66121_AUDCHST_CAT, ucIEC60958ChStat[1]); // 192, audio CATEGORY
	reg_write(priv, IT66121_AUDCHST_SRCNUM, ucIEC60958ChStat[2] & 0xF);
	reg_write(priv, IT66121_AUD0CHST_CHTNUM, (ucIEC60958ChStat[2] >> 4) & 0xF);
	reg_write(priv, IT66121_AUDCHST_CA_FS, ucIEC60958ChStat[3]); // choose clock
	reg_write(priv, IT66121_AUDCHST_OFS_WL, ucIEC60958ChStat[4]);
	it66121_switch_bank(priv, 0);
}

static void it66121_set_HBRAudio(struct it66121 *priv)
{
	u8 udata;
	it66121_switch_bank(priv, 0);

	reg_write(priv, IT66121_AUDIO_CTRL1, 0x47); // regE1 bOutputAudioMode should be loaded from ROM image.
	reg_write(priv, IT66121_AUDIO_FIFOMAP, 0xE4); // default mapping.

	if (CONFIG_INPUT_AUDIO_SPDIF) {
		reg_write(priv, IT66121_AUDIO_CTRL0, M_TX_AUD_BIT | B_TX_AUD_SPDIF);
		reg_write(priv, IT66121_AUDIO_CTRL3, B_TX_CHSTSEL);
	} else {
		reg_write(priv, IT66121_AUDIO_CTRL0, M_TX_AUD_BIT);
		reg_write(priv, IT66121_AUDIO_CTRL3, 0);
	}
	reg_write(priv, IT66121_AUD_SRCVALID_FLAT, 0x08);
	reg_write(priv, IT66121_AUD_HDAUDIO, B_TX_HBR); // regE5 = 0 ;

	//uc = HDMITX_ReadI2C_Byte(client,IT66121_CLK_CTRL1);
	//uc &= ~M_TX_AUD_DIV ;
	//HDMITX_WriteI2C_Byte(client,IT66121_CLK_CTRL1, uc);

	if (CONFIG_INPUT_AUDIO_SPDIF) {
		u8 i;
		for (i = 0; i < 100; i++) {
			if (reg_read(priv, IT66121_CLK_STATUS1) & IT66121_CLK_STATUS1_OSF_LOCK) {
				break; // stable clock.
			}
		}
		reg_write(priv, IT66121_AUDIO_CTRL0, M_TX_AUD_BIT |
				  B_TX_AUD_SPDIF | B_TX_AUD_EN_SPDIF);
	} else {
		reg_write(priv, IT66121_AUDIO_CTRL0, M_TX_AUD_BIT |
				  B_TX_AUD_EN_I2S3 |
				  B_TX_AUD_EN_I2S2 |
				  B_TX_AUD_EN_I2S1 |
				  B_TX_AUD_EN_I2S0);
	}
	udata = reg_read(priv, 0x5c);
	udata &= ~(1 << 6);
	reg_write(priv, 0x5c, udata);

	//hdmiTxDev[0].bAudioChannelEnable = reg_read(priv, IT66121_AUDIO_CTRL0);
	// reg_write(priv,IT66121_SW_RST, rst  );
}

static void it66121_set_DSDAudio(struct it66121 *priv)
{
	// to be continue
	// u8 rst;
	// rst = reg_read(priv,IT66121_SW_RST);

	//red_write(client,IT66121_SW_RST, rst | (B_HDMITX_AUD_RST|B_TX_AREF_RST) );

	reg_write(priv, IT66121_AUDIO_CTRL1, 0x41); // regE1 bOutputAudioMode should be loaded from ROM image.
	reg_write(priv, IT66121_AUDIO_FIFOMAP, 0xE4); // default mapping.

	reg_write(priv, IT66121_AUDIO_CTRL0, M_TX_AUD_BIT);
	reg_write(priv, IT66121_AUDIO_CTRL3, 0);

	reg_write(priv, IT66121_AUD_SRCVALID_FLAT, 0x00);
	reg_write(priv, IT66121_AUD_HDAUDIO, B_TX_DSD); // regE5 = 0 ;
													 //red_write(client,IT66121_SW_RST, rst & ~(B_HDMITX_AUD_RST|B_TX_AREF_RST) );

	//uc = reg_read(priv,IT66121_CLK_CTRL1);
	//uc &= ~M_TX_AUD_DIV ;
	//red_write(client,IT66121_CLK_CTRL1, uc);

	reg_write(priv, IT66121_AUDIO_CTRL0, M_TX_AUD_BIT |
			  B_TX_AUD_EN_I2S3 |
			  B_TX_AUD_EN_I2S2 |
			  B_TX_AUD_EN_I2S1 |
			  B_TX_AUD_EN_I2S0);
}

static void it66121_set_NLPCMAudio(struct it66121 *priv)
{ // no Source Num, no I2S.
	u8 AudioEnable, AudioFormat;
	u8 i;
	AudioFormat = 0x01; // NLPCM must use standard I2S mode.
	if (CONFIG_INPUT_AUDIO_SPDIF) {
		AudioEnable = M_TX_AUD_BIT | B_TX_AUD_SPDIF;
	} else {
		AudioEnable = M_TX_AUD_BIT;
	}

	it66121_switch_bank(priv, 0);
	// HDMITX_WriteI2C_Byte(client,IT66121_AUDIO_CTRL0, M_TX_AUD_24BIT|B_TX_AUD_SPDIF);
	reg_write(priv, IT66121_AUDIO_CTRL0, AudioEnable);
	//HDMITX_AndREG_Byte(IT66121_SW_RST,~(B_HDMITX_AUD_RST|B_TX_AREF_RST));

	reg_write(priv, IT66121_AUDIO_CTRL1, 0x01); // regE1 bOutputAudioMode should be loaded from ROM image.
	reg_write(priv, IT66121_AUDIO_FIFOMAP, 0xE4); // default mapping.

#ifdef USE_SPDIF_CHSTAT
	reg_write(priv, IT66121_AUDIO_CTRL3, B_TX_CHSTSEL);
#else // not USE_SPDIF_CHSTAT
	reg_write(priv, IT66121_AUDIO_CTRL3, 0);
#endif // USE_SPDIF_CHSTAT

	reg_write(priv, IT66121_AUD_SRCVALID_FLAT, 0x00);
	reg_write(priv, IT66121_AUD_HDAUDIO, 0x00); // regE5 = 0 ;

	if (CONFIG_INPUT_AUDIO_SPDIF) {
		for (i = 0; i < 100; i++) {
			if (reg_read(priv, IT66121_CLK_STATUS1) & IT66121_CLK_STATUS1_OSF_LOCK) {
				break; // stable clock.
			}
		}
	}
	priv->AudioChannelEnable = AudioEnable;
	reg_write(priv, IT66121_AUDIO_CTRL0, AudioEnable | B_TX_AUD_EN_I2S0);
}

static void it66121_set_LPCMAudio(struct it66121 *priv,
				  u8 AudioSrcNum, u8 AudSWL)
{
	u8 AudioEnable, AudioFormat;

	AudioEnable = 0;
	AudioFormat = 0;

	switch (AudSWL) {
	case 16:
		AudioEnable |= M_TX_AUD_16BIT;
		break;
	case 18:
		AudioEnable |= M_TX_AUD_18BIT;
		break;
	case 20:
		AudioEnable |= M_TX_AUD_20BIT;
		break;
	case 24:
	default:
		AudioEnable |= M_TX_AUD_24BIT;
		break;
	}
	if (CONFIG_INPUT_AUDIO_SPDIF) {
		AudioFormat &= ~0x40;
		AudioEnable |= B_TX_AUD_SPDIF | B_TX_AUD_EN_I2S0;
	} else {
		AudioFormat |= 0x40;
		switch (AudioSrcNum) {
		case 4:
			AudioEnable |= B_TX_AUD_EN_I2S3 | B_TX_AUD_EN_I2S2 | B_TX_AUD_EN_I2S1 | B_TX_AUD_EN_I2S0;
			break;

		case 3:
			AudioEnable |= B_TX_AUD_EN_I2S2 | B_TX_AUD_EN_I2S1 | B_TX_AUD_EN_I2S0;
			break;

		case 2:
			AudioEnable |= B_TX_AUD_EN_I2S1 | B_TX_AUD_EN_I2S0;
			break;

		case 1:
		default:
			AudioFormat &= ~0x40;
			AudioEnable |= B_TX_AUD_EN_I2S0;
			break;

		}
	}

	if (AudSWL != 16)
		AudioFormat |= 0x01;

	it66121_switch_bank(priv, 0);
	reg_write(priv, IT66121_AUDIO_CTRL0, AudioEnable & 0xF0);

	// regE1 bOutputAudioMode should be loaded from ROM image.
	reg_write(priv, IT66121_AUDIO_CTRL1,
			  AudioFormat |
			  B_TX_AUDFMT_DELAY_1T_TO_WS |
			  B_TX_AUDFMT_RISE_EDGE_SAMPLE_WS
			 );


	reg_write(priv, IT66121_AUDIO_FIFOMAP, 0xE4); // default mapping.
#ifdef USE_SPDIF_CHSTAT
	if (CONFIG_INPUT_AUDIO_SPDIF) {
		reg_write(priv, IT66121_AUDIO_CTRL3, B_TX_CHSTSEL);
	} else {
		reg_write(priv, IT66121_AUDIO_CTRL3, 0);
	}
#else // not USE_SPDIF_CHSTAT
	reg_write(priv, IT66121_AUDIO_CTRL3, 0);
#endif // USE_SPDIF_CHSTAT

	reg_write(priv, IT66121_AUD_SRCVALID_FLAT, 0x00);
	reg_write(priv, IT66121_AUD_HDAUDIO, 0x00); // regE5 = 0 ;
	priv->AudioChannelEnable = AudioEnable;
	if (CONFIG_INPUT_AUDIO_SPDIF) {
		u8 i;
		write_a_entry(priv, 0x5c, (1 << 6), (1 << 6));
		for (i = 0; i < 100; i++) {
			if (reg_read(priv, IT66121_CLK_STATUS1) & IT66121_CLK_STATUS1_OSF_LOCK) {
				break; // stable clock.
			}
		}
	}
}

static int it66121_aud_output_config(struct it66121 *priv,
				     struct hdmi_codec_params *param)
{
	u8 udata;
	u8 fs;
	u8 ucIEC60958ChStat[8];

	write_a_entry(priv, IT66121_SW_RST, (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD),
				  (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD));
	reg_write(priv, IT66121_CLK_CTRL0, IT66121_CLK_CTRL0_OSCLK_AUTO | IT66121_CLK_CTRL0_MCLK_256FS | IT66121_CLK_CTRL0_IPCLK_AUTO);

	write_a_entry(priv, IT66121_SYS_STATUS1, 0x10, 0x00); // power on the ACLK

	//use i2s
	udata = reg_read(priv, IT66121_AUDIO_CTRL0);
	udata &= ~B_TX_AUD_SPDIF;
	reg_write(priv, IT66121_AUDIO_CTRL0, udata);


	// one bit audio have no channel status.
	switch (param->sample_rate) {
	case  44100L:
		fs =  AUDFS_44p1KHz; break;
	case  88200L:
		fs =  AUDFS_88p2KHz; break;
	case 176400L:
		fs = AUDFS_176p4KHz; break;
	case  32000L:
		fs =    AUDFS_32KHz; break;
	case  48000L:
		fs =    AUDFS_48KHz; break;
	case  96000L:
		fs =    AUDFS_96KHz; break;
	case 192000L:
		fs =   AUDFS_192KHz; break;
	case 768000L:
		fs =   AUDFS_768KHz; break;
	default:
		//SampleFreq = 48000L;
		fs =    AUDFS_48KHz;
		break; // default, set Fs = 48KHz.
	}
	it66121_aud_set_fs(priv, fs);

	ucIEC60958ChStat[0] = 0;
	ucIEC60958ChStat[1] = 0;
	ucIEC60958ChStat[2] = (param->channels + 1) / 2;

	if (ucIEC60958ChStat[2] < 1) {
		ucIEC60958ChStat[2] = 1;
	} else if (ucIEC60958ChStat[2] > 4) {
		ucIEC60958ChStat[2] = 4;
	}
	ucIEC60958ChStat[3] = fs;
	ucIEC60958ChStat[4] = (((~fs) << 4) & 0xF0) | CHTSTS_SWCODE; // Fs | 24bit word length

	write_a_entry(priv, IT66121_SW_RST, (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD), IT66121_SW_RST_SOFT_AUD);

	switch (CNOFIG_INPUT_AUDIO_TYPE) {
	case T_AUDIO_HBR:
		ucIEC60958ChStat[0] |= 1 << 1;
		ucIEC60958ChStat[2] = 0;
		ucIEC60958ChStat[3] &= 0xF0;
		ucIEC60958ChStat[3] |= AUDFS_768KHz;
		ucIEC60958ChStat[4] |= (((~AUDFS_768KHz) << 4) & 0xF0) | 0xB;
		it66121_set_ChStat(priv, ucIEC60958ChStat);
		it66121_set_HBRAudio(priv);

		break;
	case T_AUDIO_DSD:
		it66121_set_DSDAudio(priv);
		break;
	case T_AUDIO_NLPCM:
		ucIEC60958ChStat[0] |= 1 << 1;
		it66121_set_ChStat(priv, ucIEC60958ChStat);
		it66121_set_NLPCMAudio(priv);
		break;
	case T_AUDIO_LPCM:
		ucIEC60958ChStat[0] &= ~(1 << 1);

		it66121_set_ChStat(priv, ucIEC60958ChStat);
		it66121_set_LPCMAudio(priv, (param->channels + 1) / 2, SUPPORT_AUDI_AudSWL);
		// can add auto adjust
		break;
	}
	udata = reg_read(priv, IT66121_INT_MASK1);
	udata &= ~IT66121_INT_MASK1_AUDIO_OVERFLOW;
	reg_write(priv, IT66121_INT_MASK1, udata);
	reg_write(priv, IT66121_AUDIO_CTRL0, priv->AudioChannelEnable);

	write_a_entry(priv, IT66121_SW_RST, (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD), 0);
	return 0;
}

static int it66121_audio_hw_params(struct device *dev, void *data,
				   struct hdmi_codec_daifmt *daifmt,
				   struct hdmi_codec_params *params)
{
	struct it66121 *priv = dev_get_drvdata(dev);

	dev_err(&priv->i2c->dev, "%s: %u Hz, %d bit, %d channels\n", __func__,
			params->sample_rate, params->sample_width, params->channels);

	it66121_aud_config_aai(priv);
	it66121_aud_output_config(priv, params);
	return 0;
}

static void it66121_audio_shutdown(struct device *dev, void *data)
{
}

static int it66121_audio_digital_mute(struct device *dev, void *data, bool enable)
{
	struct it66121 *priv = dev_get_drvdata(dev);

	return 0;
}

static int it66121_audio_get_eld(struct device *dev, void *data,
				 uint8_t *buf, size_t len)
{
	struct it66121 *priv = dev_get_drvdata(dev);

	memcpy(buf, priv->connector.eld, min(sizeof(priv->connector.eld), len));

	return 0;
}

static const struct hdmi_codec_ops audio_codec_ops = {
	.hw_params = it66121_audio_hw_params,
	.audio_shutdown = it66121_audio_shutdown,
	.digital_mute = it66121_audio_digital_mute,
	.get_eld = it66121_audio_get_eld,
};

static int it66121_audio_codec_init(struct it66121 *priv,
				    struct device *dev)
{
	struct hdmi_codec_pdata codec_data = {
		.ops = &audio_codec_ops,
		.max_i2s_channels = 2,
		.i2s = 1,
	};

	priv->audio_pdev = platform_device_register_data(
		dev, HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_AUTO,
		&codec_data, sizeof(codec_data));

	return PTR_ERR_OR_ZERO(priv->audio_pdev);
}

static void it66121_free(struct it66121 *priv)
{
	if (priv->audio_pdev)
		platform_device_unregister(priv->audio_pdev);
}

static int it66121_init(struct i2c_client *client, struct it66121 *priv)
{
	int ret = 0;

	if (it66121_load_reg_table(priv, it66121_init_table) < 0) {
		goto err_device;
	}

	if (it66121_load_reg_table(priv, it66121_default_video_table) < 0) {
		goto err_device;
	}

	if (it66121_load_reg_table(priv, it66121_setHDMI_table) < 0) {
		goto err_device;
	}

	if (it66121_load_reg_table(priv, it66121_default_AVI_info_table) < 0) {
		goto err_device;
	}

	if (it66121_load_reg_table(priv, it66121_default_audio_info_table) < 0) {
		goto err_device;
	}

	if (it66121_load_reg_table(priv, it66121_aud_CHStatus_LPCM_20bit_48Khz) < 0) {
		goto err_device;
	}

	if (it66121_load_reg_table(priv, it66121_AUD_SPDIF_2ch_24bit) < 0) {
		goto err_device;
	}

	if (it66121_audio_codec_init(priv, &client->dev) != 0) {
		dev_err(&priv->i2c->dev, "fail to init hdmi audio\n");
		goto err_device;
	}


	return ret;

err_device:
	it66121_free(priv);
	return -ENXIO;;
}

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

	priv->regmap = devm_regmap_init_i2c(client, &it66121_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset_gpio)) {
		dev_err(dev, "Failed to retrieve/request reset gpio: %ld\n",
			PTR_ERR(priv->reset_gpio));
		return PTR_ERR(priv->reset_gpio);
	}

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

	ret = it66121_init(client, priv);
	if (ret < 0) {
		dev_err(dev, "it66121_init error \n");
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

	priv->bridge.funcs = &it66121_bridge_funcs;
	priv->bridge.of_node = client->dev.of_node;
	drm_bridge_add(&priv->bridge);

	return 0;
}

static int it66121_remove(struct i2c_client *client)
{
	struct it66121 *priv = i2c_get_clientdata(client);

	drm_bridge_remove(&priv->bridge);

	it66121_free(priv);

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
	},
	.id_table = it66121_ids,
};
module_i2c_driver(it66121_driver);

MODULE_AUTHOR("Baozhu Zuo <zuobaozhu@gmail.com>");
MODULE_DESCRIPTION("IT66121 RGB-HDMI bridge");
MODULE_LICENSE("GPL v2");
