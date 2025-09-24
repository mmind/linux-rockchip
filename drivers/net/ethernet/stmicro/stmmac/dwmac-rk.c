// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * DOC: dwmac-rk.c - Rockchip RK3288 DWMAC specific glue layer
 *
 * Copyright (C) 2014 Chen-Zhi (Roger Chen)
 *
 * Chen-Zhi (Roger Chen)  <roger.chen@rock-chips.com>
 */

#include <linux/stmmac.h>
#include <linux/hw_bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/phy.h>
#include <linux/of_net.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

#include "stmmac_platform.h"

#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/pkt_cls.h>
#include <net/tcp.h>
#include <net/udp.h>
#include "stmmac.h"
#include "dwmac1000.h"
#include "dwmac_dma.h"

struct rk_priv_data;

struct rk_reg_speed_data {
	unsigned int rgmii_10;
	unsigned int rgmii_100;
	unsigned int rgmii_1000;
	unsigned int rmii_10;
	unsigned int rmii_100;
};

struct rk_gmac_ops {
	void (*set_to_rgmii)(struct rk_priv_data *bsp_priv,
			     int tx_delay, int rx_delay);
	void (*set_to_rmii)(struct rk_priv_data *bsp_priv);
	int (*set_speed)(struct rk_priv_data *bsp_priv,
			 phy_interface_t interface, int speed);
	void (*set_clock_selection)(struct rk_priv_data *bsp_priv, bool input,
				    bool enable);
	void (*integrated_phy_powerup)(struct rk_priv_data *bsp_priv);
	void (*integrated_phy_powerdown)(struct rk_priv_data *bsp_priv);
	bool php_grf_required;
	bool regs_valid;
	u32 regs[];
};

static const char * const rk_clocks[] = {
	"aclk_mac", "pclk_mac", "mac_clk_tx", "clk_mac_speed",
};

static const char * const rk_rmii_clocks[] = {
	"mac_clk_rx", "clk_mac_ref", "clk_mac_refout",
};

enum rk_clocks_index {
	RK_ACLK_MAC = 0,
	RK_PCLK_MAC,
	RK_MAC_CLK_TX,
	RK_CLK_MAC_SPEED,
	RK_MAC_CLK_RX,
	RK_CLK_MAC_REF,
	RK_CLK_MAC_REFOUT,
};

struct rk_priv_data {
	struct device *dev;
	phy_interface_t phy_iface;
	int id;
	struct regulator *regulator;
	const struct rk_gmac_ops *ops;

	bool clk_enabled;
	bool clock_input;
	bool integrated_phy;

	struct clk_bulk_data *clks;
	int num_clks;
	struct clk *clk_phy;

	struct reset_control *phy_reset;

	int tx_delay;
	int rx_delay;

	struct regmap *grf;
	struct regmap *php_grf;
};

static int rk_set_reg_speed(struct rk_priv_data *bsp_priv,
			    const struct rk_reg_speed_data *rsd,
			    unsigned int reg, phy_interface_t interface,
			    int speed)
{
	unsigned int val;

	if (phy_interface_mode_is_rgmii(interface)) {
		if (speed == SPEED_10) {
			val = rsd->rgmii_10;
		} else if (speed == SPEED_100) {
			val = rsd->rgmii_100;
		} else if (speed == SPEED_1000) {
			val = rsd->rgmii_1000;
		} else {
			/* Phylink will not allow inappropriate speeds for
			 * interface modes, so this should never happen.
			 */
			return -EINVAL;
		}
	} else if (interface == PHY_INTERFACE_MODE_RMII) {
		if (speed == SPEED_10) {
			val = rsd->rmii_10;
		} else if (speed == SPEED_100) {
			val = rsd->rmii_100;
		} else {
			/* Phylink will not allow inappropriate speeds for
			 * interface modes, so this should never happen.
			 */
			return -EINVAL;
		}
	} else {
		/* This should never happen, as .get_interfaces() limits
		 * the interface modes that are supported to RGMII and/or
		 * RMII.
		 */
		return -EINVAL;
	}

	regmap_write(bsp_priv->grf, reg, val);

	return 0;

}

static int rk_set_clk_mac_speed(struct rk_priv_data *bsp_priv,
				phy_interface_t interface, int speed)
{
	struct clk *clk_mac_speed = bsp_priv->clks[RK_CLK_MAC_SPEED].clk;
	long rate;

	rate = rgmii_clock(speed);
	if (rate < 0)
		return rate;

	return clk_set_rate(clk_mac_speed, rate);
}

#define HIWORD_UPDATE(val, mask, shift) \
		(FIELD_PREP_WM16((mask) << (shift), (val)))

#define GRF_BIT(nr)	(BIT(nr) | BIT(nr+16))
#define GRF_CLR_BIT(nr)	(BIT(nr+16))

#define DELAY_ENABLE(soc, tx, rx) \
	(((tx) ? soc##_GMAC_TXCLK_DLY_ENABLE : soc##_GMAC_TXCLK_DLY_DISABLE) | \
	 ((rx) ? soc##_GMAC_RXCLK_DLY_ENABLE : soc##_GMAC_RXCLK_DLY_DISABLE))

#define RK_GRF_MACPHY_CON0		0xb00
#define RK_GRF_MACPHY_CON1		0xb04
#define RK_GRF_MACPHY_CON2		0xb08
#define RK_GRF_MACPHY_CON3		0xb0c

#define RK_MACPHY_ENABLE		GRF_BIT(0)
#define RK_MACPHY_DISABLE		GRF_CLR_BIT(0)
#define RK_MACPHY_CFG_CLK_50M		GRF_BIT(14)
#define RK_GMAC2PHY_RMII_MODE		(GRF_BIT(6) | GRF_CLR_BIT(7))
#define RK_GRF_CON2_MACPHY_ID		HIWORD_UPDATE(0x1234, 0xffff, 0)
#define RK_GRF_CON3_MACPHY_ID		HIWORD_UPDATE(0x35, 0x3f, 0)

static void rk_gmac_integrated_ephy_powerup(struct rk_priv_data *priv)
{
	regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_MACPHY_CFG_CLK_50M);
	regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_GMAC2PHY_RMII_MODE);

	regmap_write(priv->grf, RK_GRF_MACPHY_CON2, RK_GRF_CON2_MACPHY_ID);
	regmap_write(priv->grf, RK_GRF_MACPHY_CON3, RK_GRF_CON3_MACPHY_ID);

	if (priv->phy_reset) {
		/* PHY needs to be disabled before trying to reset it */
		regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_MACPHY_DISABLE);
		if (priv->phy_reset)
			reset_control_assert(priv->phy_reset);
		usleep_range(10, 20);
		if (priv->phy_reset)
			reset_control_deassert(priv->phy_reset);
		usleep_range(10, 20);
		regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_MACPHY_ENABLE);
		msleep(30);
	}
}

static void rk_gmac_integrated_ephy_powerdown(struct rk_priv_data *priv)
{
	regmap_write(priv->grf, RK_GRF_MACPHY_CON0, RK_MACPHY_DISABLE);
	if (priv->phy_reset)
		reset_control_assert(priv->phy_reset);
}

#define RK_FEPHY_SHUTDOWN		GRF_BIT(1)
#define RK_FEPHY_POWERUP		GRF_CLR_BIT(1)
#define RK_FEPHY_INTERNAL_RMII_SEL	GRF_BIT(6)
#define RK_FEPHY_24M_CLK_SEL		(GRF_BIT(8) | GRF_BIT(9))
#define RK_FEPHY_PHY_ID			GRF_BIT(11)

static void rk_gmac_integrated_fephy_powerup(struct rk_priv_data *priv,
					     unsigned int reg)
{
	reset_control_assert(priv->phy_reset);
	usleep_range(20, 30);

	regmap_write(priv->grf, reg,
		     RK_FEPHY_POWERUP |
		     RK_FEPHY_INTERNAL_RMII_SEL |
		     RK_FEPHY_24M_CLK_SEL |
		     RK_FEPHY_PHY_ID);
	usleep_range(10000, 12000);

	reset_control_deassert(priv->phy_reset);
	usleep_range(50000, 60000);
}

static void rk_gmac_integrated_fephy_powerdown(struct rk_priv_data *priv,
					       unsigned int reg)
{
	regmap_write(priv->grf, reg, RK_FEPHY_SHUTDOWN);
}

#define PX30_GRF_GMAC_CON1		0x0904

/* PX30_GRF_GMAC_CON1 */
#define PX30_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(4) | GRF_CLR_BIT(5) | \
					 GRF_BIT(6))
#define PX30_GMAC_SPEED_10M		GRF_CLR_BIT(2)
#define PX30_GMAC_SPEED_100M		GRF_BIT(2)

static void px30_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, PX30_GRF_GMAC_CON1,
		     PX30_GMAC_PHY_INTF_SEL_RMII);
}

static int px30_set_speed(struct rk_priv_data *bsp_priv,
			  phy_interface_t interface, int speed)
{
	struct clk *clk_mac_speed = bsp_priv->clks[RK_CLK_MAC_SPEED].clk;
	struct device *dev = bsp_priv->dev;
	unsigned int con1;
	long rate;

	if (!clk_mac_speed) {
		dev_err(dev, "%s: Missing clk_mac_speed clock\n", __func__);
		return -EINVAL;
	}

	if (speed == 10) {
		con1 = PX30_GMAC_SPEED_10M;
		rate = 2500000;
	} else if (speed == 100) {
		con1 = PX30_GMAC_SPEED_100M;
		rate = 25000000;
	} else {
		dev_err(dev, "unknown speed value for RMII! speed=%d", speed);
		return -EINVAL;
	}

	regmap_write(bsp_priv->grf, PX30_GRF_GMAC_CON1, con1);

	return clk_set_rate(clk_mac_speed, rate);
}

static const struct rk_gmac_ops px30_ops = {
	.set_to_rmii = px30_set_to_rmii,
	.set_speed = px30_set_speed,
};

#define RK3128_GRF_MAC_CON0	0x0168
#define RK3128_GRF_MAC_CON1	0x016c

/* RK3128_GRF_MAC_CON0 */
#define RK3128_GMAC_TXCLK_DLY_ENABLE   GRF_BIT(14)
#define RK3128_GMAC_TXCLK_DLY_DISABLE  GRF_CLR_BIT(14)
#define RK3128_GMAC_RXCLK_DLY_ENABLE   GRF_BIT(15)
#define RK3128_GMAC_RXCLK_DLY_DISABLE  GRF_CLR_BIT(15)
#define RK3128_GMAC_CLK_RX_DL_CFG(val) HIWORD_UPDATE(val, 0x7F, 7)
#define RK3128_GMAC_CLK_TX_DL_CFG(val) HIWORD_UPDATE(val, 0x7F, 0)

/* RK3128_GRF_MAC_CON1 */
#define RK3128_GMAC_PHY_INTF_SEL_RGMII	\
		(GRF_BIT(6) | GRF_CLR_BIT(7) | GRF_CLR_BIT(8))
#define RK3128_GMAC_PHY_INTF_SEL_RMII	\
		(GRF_CLR_BIT(6) | GRF_CLR_BIT(7) | GRF_BIT(8))
#define RK3128_GMAC_FLOW_CTRL          GRF_BIT(9)
#define RK3128_GMAC_FLOW_CTRL_CLR      GRF_CLR_BIT(9)
#define RK3128_GMAC_SPEED_10M          GRF_CLR_BIT(10)
#define RK3128_GMAC_SPEED_100M         GRF_BIT(10)
#define RK3128_GMAC_RMII_CLK_25M       GRF_BIT(11)
#define RK3128_GMAC_RMII_CLK_2_5M      GRF_CLR_BIT(11)
#define RK3128_GMAC_CLK_125M           (GRF_CLR_BIT(12) | GRF_CLR_BIT(13))
#define RK3128_GMAC_CLK_25M            (GRF_BIT(12) | GRF_BIT(13))
#define RK3128_GMAC_CLK_2_5M           (GRF_CLR_BIT(12) | GRF_BIT(13))
#define RK3128_GMAC_RMII_MODE          GRF_BIT(14)
#define RK3128_GMAC_RMII_MODE_CLR      GRF_CLR_BIT(14)

static void rk3128_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3128_GRF_MAC_CON1,
		     RK3128_GMAC_PHY_INTF_SEL_RGMII |
		     RK3128_GMAC_RMII_MODE_CLR);
	regmap_write(bsp_priv->grf, RK3128_GRF_MAC_CON0,
		     DELAY_ENABLE(RK3128, tx_delay, rx_delay) |
		     RK3128_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3128_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3128_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3128_GRF_MAC_CON1,
		     RK3128_GMAC_PHY_INTF_SEL_RMII | RK3128_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3128_reg_speed_data = {
	.rgmii_10 = RK3128_GMAC_CLK_2_5M,
	.rgmii_100 = RK3128_GMAC_CLK_25M,
	.rgmii_1000 = RK3128_GMAC_CLK_125M,
	.rmii_10 = RK3128_GMAC_RMII_CLK_2_5M | RK3128_GMAC_SPEED_10M,
	.rmii_100 = RK3128_GMAC_RMII_CLK_25M | RK3128_GMAC_SPEED_100M,
};

static int rk3128_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3128_reg_speed_data,
				RK3128_GRF_MAC_CON1, interface, speed);
}

static const struct rk_gmac_ops rk3128_ops = {
	.set_to_rgmii = rk3128_set_to_rgmii,
	.set_to_rmii = rk3128_set_to_rmii,
	.set_speed = rk3128_set_speed,
};

#define RK3228_GRF_MAC_CON0	0x0900
#define RK3228_GRF_MAC_CON1	0x0904

#define RK3228_GRF_CON_MUX	0x50

/* RK3228_GRF_MAC_CON0 */
#define RK3228_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 7)
#define RK3228_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

/* RK3228_GRF_MAC_CON1 */
#define RK3228_GMAC_PHY_INTF_SEL_RGMII	\
		(GRF_BIT(4) | GRF_CLR_BIT(5) | GRF_CLR_BIT(6))
#define RK3228_GMAC_PHY_INTF_SEL_RMII	\
		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5) | GRF_BIT(6))
#define RK3228_GMAC_FLOW_CTRL		GRF_BIT(3)
#define RK3228_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(3)
#define RK3228_GMAC_SPEED_10M		GRF_CLR_BIT(2)
#define RK3228_GMAC_SPEED_100M		GRF_BIT(2)
#define RK3228_GMAC_RMII_CLK_25M	GRF_BIT(7)
#define RK3228_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(7)
#define RK3228_GMAC_CLK_125M		(GRF_CLR_BIT(8) | GRF_CLR_BIT(9))
#define RK3228_GMAC_CLK_25M		(GRF_BIT(8) | GRF_BIT(9))
#define RK3228_GMAC_CLK_2_5M		(GRF_CLR_BIT(8) | GRF_BIT(9))
#define RK3228_GMAC_RMII_MODE		GRF_BIT(10)
#define RK3228_GMAC_RMII_MODE_CLR	GRF_CLR_BIT(10)
#define RK3228_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(0)
#define RK3228_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(0)
#define RK3228_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(1)
#define RK3228_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(1)

/* RK3228_GRF_COM_MUX */
#define RK3228_GRF_CON_MUX_GMAC_INTEGRATED_PHY	GRF_BIT(15)

static void rk3228_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3228_GRF_MAC_CON1,
		     RK3228_GMAC_PHY_INTF_SEL_RGMII |
		     RK3228_GMAC_RMII_MODE_CLR |
		     DELAY_ENABLE(RK3228, tx_delay, rx_delay));

	regmap_write(bsp_priv->grf, RK3228_GRF_MAC_CON0,
		     RK3228_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3228_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3228_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3228_GRF_MAC_CON1,
		     RK3228_GMAC_PHY_INTF_SEL_RMII |
		     RK3228_GMAC_RMII_MODE);

	/* set MAC to RMII mode */
	regmap_write(bsp_priv->grf, RK3228_GRF_MAC_CON1, GRF_BIT(11));
}

static const struct rk_reg_speed_data rk3228_reg_speed_data = {
	.rgmii_10 = RK3228_GMAC_CLK_2_5M,
	.rgmii_100 = RK3228_GMAC_CLK_25M,
	.rgmii_1000 = RK3228_GMAC_CLK_125M,
	.rmii_10 = RK3228_GMAC_RMII_CLK_2_5M | RK3228_GMAC_SPEED_10M,
	.rmii_100 = RK3228_GMAC_RMII_CLK_25M | RK3228_GMAC_SPEED_100M,
};

static int rk3228_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3228_reg_speed_data,
				RK3228_GRF_MAC_CON1, interface, speed);
}

static void rk3228_integrated_phy_powerup(struct rk_priv_data *priv)
{
	regmap_write(priv->grf, RK3228_GRF_CON_MUX,
		     RK3228_GRF_CON_MUX_GMAC_INTEGRATED_PHY);

	rk_gmac_integrated_ephy_powerup(priv);
}

static const struct rk_gmac_ops rk3228_ops = {
	.set_to_rgmii = rk3228_set_to_rgmii,
	.set_to_rmii = rk3228_set_to_rmii,
	.set_speed = rk3228_set_speed,
	.integrated_phy_powerup = rk3228_integrated_phy_powerup,
	.integrated_phy_powerdown = rk_gmac_integrated_ephy_powerdown,
};

#define RK3288_GRF_SOC_CON1	0x0248
#define RK3288_GRF_SOC_CON3	0x0250

/*RK3288_GRF_SOC_CON1*/
#define RK3288_GMAC_PHY_INTF_SEL_RGMII	(GRF_BIT(6) | GRF_CLR_BIT(7) | \
					 GRF_CLR_BIT(8))
#define RK3288_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(6) | GRF_CLR_BIT(7) | \
					 GRF_BIT(8))
#define RK3288_GMAC_FLOW_CTRL		GRF_BIT(9)
#define RK3288_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(9)
#define RK3288_GMAC_SPEED_10M		GRF_CLR_BIT(10)
#define RK3288_GMAC_SPEED_100M		GRF_BIT(10)
#define RK3288_GMAC_RMII_CLK_25M	GRF_BIT(11)
#define RK3288_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(11)
#define RK3288_GMAC_CLK_125M		(GRF_CLR_BIT(12) | GRF_CLR_BIT(13))
#define RK3288_GMAC_CLK_25M		(GRF_BIT(12) | GRF_BIT(13))
#define RK3288_GMAC_CLK_2_5M		(GRF_CLR_BIT(12) | GRF_BIT(13))
#define RK3288_GMAC_RMII_MODE		GRF_BIT(14)
#define RK3288_GMAC_RMII_MODE_CLR	GRF_CLR_BIT(14)

/*RK3288_GRF_SOC_CON3*/
#define RK3288_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(14)
#define RK3288_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(14)
#define RK3288_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3288_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3288_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 7)
#define RK3288_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void rk3288_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
		     RK3288_GMAC_PHY_INTF_SEL_RGMII |
		     RK3288_GMAC_RMII_MODE_CLR);
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON3,
		     DELAY_ENABLE(RK3288, tx_delay, rx_delay) |
		     RK3288_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3288_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3288_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3288_GRF_SOC_CON1,
		     RK3288_GMAC_PHY_INTF_SEL_RMII | RK3288_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3288_reg_speed_data = {
	.rgmii_10 = RK3288_GMAC_CLK_2_5M,
	.rgmii_100 = RK3288_GMAC_CLK_25M,
	.rgmii_1000 = RK3288_GMAC_CLK_125M,
	.rmii_10 = RK3288_GMAC_RMII_CLK_2_5M | RK3288_GMAC_SPEED_10M,
	.rmii_100 = RK3288_GMAC_RMII_CLK_25M | RK3288_GMAC_SPEED_100M,
};

static int rk3288_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3288_reg_speed_data,
				RK3288_GRF_SOC_CON1, interface, speed);
}

static const struct rk_gmac_ops rk3288_ops = {
	.set_to_rgmii = rk3288_set_to_rgmii,
	.set_to_rmii = rk3288_set_to_rmii,
	.set_speed = rk3288_set_speed,
};

#define RK3308_GRF_MAC_CON0		0x04a0

/* RK3308_GRF_MAC_CON0 */
#define RK3308_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(2) | GRF_CLR_BIT(3) | \
					GRF_BIT(4))
#define RK3308_GMAC_FLOW_CTRL		GRF_BIT(3)
#define RK3308_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(3)
#define RK3308_GMAC_SPEED_10M		GRF_CLR_BIT(0)
#define RK3308_GMAC_SPEED_100M		GRF_BIT(0)

static void rk3308_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3308_GRF_MAC_CON0,
		     RK3308_GMAC_PHY_INTF_SEL_RMII);
}

static const struct rk_reg_speed_data rk3308_reg_speed_data = {
	.rmii_10 = RK3308_GMAC_SPEED_10M,
	.rmii_100 = RK3308_GMAC_SPEED_100M,
};

static int rk3308_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3308_reg_speed_data,
				RK3308_GRF_MAC_CON0, interface, speed);
}

static const struct rk_gmac_ops rk3308_ops = {
	.set_to_rmii = rk3308_set_to_rmii,
	.set_speed = rk3308_set_speed,
};

#define RK3328_GRF_MAC_CON0	0x0900
#define RK3328_GRF_MAC_CON1	0x0904
#define RK3328_GRF_MAC_CON2	0x0908
#define RK3328_GRF_MACPHY_CON1	0xb04

/* RK3328_GRF_MAC_CON0 */
#define RK3328_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 7)
#define RK3328_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

/* RK3328_GRF_MAC_CON1 */
#define RK3328_GMAC_PHY_INTF_SEL_RGMII	\
		(GRF_BIT(4) | GRF_CLR_BIT(5) | GRF_CLR_BIT(6))
#define RK3328_GMAC_PHY_INTF_SEL_RMII	\
		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5) | GRF_BIT(6))
#define RK3328_GMAC_FLOW_CTRL		GRF_BIT(3)
#define RK3328_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(3)
#define RK3328_GMAC_SPEED_10M		GRF_CLR_BIT(2)
#define RK3328_GMAC_SPEED_100M		GRF_BIT(2)
#define RK3328_GMAC_RMII_CLK_25M	GRF_BIT(7)
#define RK3328_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(7)
#define RK3328_GMAC_CLK_125M		(GRF_CLR_BIT(11) | GRF_CLR_BIT(12))
#define RK3328_GMAC_CLK_25M		(GRF_BIT(11) | GRF_BIT(12))
#define RK3328_GMAC_CLK_2_5M		(GRF_CLR_BIT(11) | GRF_BIT(12))
#define RK3328_GMAC_RMII_MODE		GRF_BIT(9)
#define RK3328_GMAC_RMII_MODE_CLR	GRF_CLR_BIT(9)
#define RK3328_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(0)
#define RK3328_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(1)

/* RK3328_GRF_MACPHY_CON1 */
#define RK3328_MACPHY_RMII_MODE		GRF_BIT(9)

static void rk3328_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3328_GRF_MAC_CON1,
		     RK3328_GMAC_PHY_INTF_SEL_RGMII |
		     RK3328_GMAC_RMII_MODE_CLR |
		     RK3328_GMAC_RXCLK_DLY_ENABLE |
		     RK3328_GMAC_TXCLK_DLY_ENABLE);

	regmap_write(bsp_priv->grf, RK3328_GRF_MAC_CON0,
		     RK3328_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3328_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3328_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	unsigned int reg;

	reg = bsp_priv->integrated_phy ? RK3328_GRF_MAC_CON2 :
		  RK3328_GRF_MAC_CON1;

	regmap_write(bsp_priv->grf, reg,
		     RK3328_GMAC_PHY_INTF_SEL_RMII |
		     RK3328_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3328_reg_speed_data = {
	.rgmii_10 = RK3328_GMAC_CLK_2_5M,
	.rgmii_100 = RK3328_GMAC_CLK_25M,
	.rgmii_1000 = RK3328_GMAC_CLK_125M,
	.rmii_10 = RK3328_GMAC_RMII_CLK_2_5M | RK3328_GMAC_SPEED_10M,
	.rmii_100 = RK3328_GMAC_RMII_CLK_25M | RK3328_GMAC_SPEED_100M,
};

static int rk3328_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	unsigned int reg;

	if (interface == PHY_INTERFACE_MODE_RMII && bsp_priv->integrated_phy)
		reg = RK3328_GRF_MAC_CON2;
	else
		reg = RK3328_GRF_MAC_CON1;

	return rk_set_reg_speed(bsp_priv, &rk3328_reg_speed_data, reg,
				interface, speed);
}

static void rk3328_integrated_phy_powerup(struct rk_priv_data *priv)
{
	regmap_write(priv->grf, RK3328_GRF_MACPHY_CON1,
		     RK3328_MACPHY_RMII_MODE);

	rk_gmac_integrated_ephy_powerup(priv);
}

static const struct rk_gmac_ops rk3328_ops = {
	.set_to_rgmii = rk3328_set_to_rgmii,
	.set_to_rmii = rk3328_set_to_rmii,
	.set_speed = rk3328_set_speed,
	.integrated_phy_powerup = rk3328_integrated_phy_powerup,
	.integrated_phy_powerdown = rk_gmac_integrated_ephy_powerdown,
};

#define RK3366_GRF_SOC_CON6	0x0418
#define RK3366_GRF_SOC_CON7	0x041c

/* RK3366_GRF_SOC_CON6 */
#define RK3366_GMAC_PHY_INTF_SEL_RGMII	(GRF_BIT(9) | GRF_CLR_BIT(10) | \
					 GRF_CLR_BIT(11))
#define RK3366_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(9) | GRF_CLR_BIT(10) | \
					 GRF_BIT(11))
#define RK3366_GMAC_FLOW_CTRL		GRF_BIT(8)
#define RK3366_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(8)
#define RK3366_GMAC_SPEED_10M		GRF_CLR_BIT(7)
#define RK3366_GMAC_SPEED_100M		GRF_BIT(7)
#define RK3366_GMAC_RMII_CLK_25M	GRF_BIT(3)
#define RK3366_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(3)
#define RK3366_GMAC_CLK_125M		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5))
#define RK3366_GMAC_CLK_25M		(GRF_BIT(4) | GRF_BIT(5))
#define RK3366_GMAC_CLK_2_5M		(GRF_CLR_BIT(4) | GRF_BIT(5))
#define RK3366_GMAC_RMII_MODE		GRF_BIT(6)
#define RK3366_GMAC_RMII_MODE_CLR	GRF_CLR_BIT(6)

/* RK3366_GRF_SOC_CON7 */
#define RK3366_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(7)
#define RK3366_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(7)
#define RK3366_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3366_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3366_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 8)
#define RK3366_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void rk3366_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3366_GRF_SOC_CON6,
		     RK3366_GMAC_PHY_INTF_SEL_RGMII |
		     RK3366_GMAC_RMII_MODE_CLR);
	regmap_write(bsp_priv->grf, RK3366_GRF_SOC_CON7,
		     DELAY_ENABLE(RK3366, tx_delay, rx_delay) |
		     RK3366_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3366_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3366_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3366_GRF_SOC_CON6,
		     RK3366_GMAC_PHY_INTF_SEL_RMII | RK3366_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3366_reg_speed_data = {
	.rgmii_10 = RK3366_GMAC_CLK_2_5M,
	.rgmii_100 = RK3366_GMAC_CLK_25M,
	.rgmii_1000 = RK3366_GMAC_CLK_125M,
	.rmii_10 = RK3366_GMAC_RMII_CLK_2_5M | RK3366_GMAC_SPEED_10M,
	.rmii_100 = RK3366_GMAC_RMII_CLK_25M | RK3366_GMAC_SPEED_100M,
};

static int rk3366_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3366_reg_speed_data,
				RK3366_GRF_SOC_CON6, interface, speed);
}

static const struct rk_gmac_ops rk3366_ops = {
	.set_to_rgmii = rk3366_set_to_rgmii,
	.set_to_rmii = rk3366_set_to_rmii,
	.set_speed = rk3366_set_speed,
};

#define RK3368_GRF_SOC_CON15	0x043c
#define RK3368_GRF_SOC_CON16	0x0440

/* RK3368_GRF_SOC_CON15 */
#define RK3368_GMAC_PHY_INTF_SEL_RGMII	(GRF_BIT(9) | GRF_CLR_BIT(10) | \
					 GRF_CLR_BIT(11))
#define RK3368_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(9) | GRF_CLR_BIT(10) | \
					 GRF_BIT(11))
#define RK3368_GMAC_FLOW_CTRL		GRF_BIT(8)
#define RK3368_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(8)
#define RK3368_GMAC_SPEED_10M		GRF_CLR_BIT(7)
#define RK3368_GMAC_SPEED_100M		GRF_BIT(7)
#define RK3368_GMAC_RMII_CLK_25M	GRF_BIT(3)
#define RK3368_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(3)
#define RK3368_GMAC_CLK_125M		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5))
#define RK3368_GMAC_CLK_25M		(GRF_BIT(4) | GRF_BIT(5))
#define RK3368_GMAC_CLK_2_5M		(GRF_CLR_BIT(4) | GRF_BIT(5))
#define RK3368_GMAC_RMII_MODE		GRF_BIT(6)
#define RK3368_GMAC_RMII_MODE_CLR	GRF_CLR_BIT(6)

/* RK3368_GRF_SOC_CON16 */
#define RK3368_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(7)
#define RK3368_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(7)
#define RK3368_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3368_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3368_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 8)
#define RK3368_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void rk3368_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3368_GRF_SOC_CON15,
		     RK3368_GMAC_PHY_INTF_SEL_RGMII |
		     RK3368_GMAC_RMII_MODE_CLR);
	regmap_write(bsp_priv->grf, RK3368_GRF_SOC_CON16,
		     DELAY_ENABLE(RK3368, tx_delay, rx_delay) |
		     RK3368_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3368_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3368_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3368_GRF_SOC_CON15,
		     RK3368_GMAC_PHY_INTF_SEL_RMII | RK3368_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3368_reg_speed_data = {
	.rgmii_10 = RK3368_GMAC_CLK_2_5M,
	.rgmii_100 = RK3368_GMAC_CLK_25M,
	.rgmii_1000 = RK3368_GMAC_CLK_125M,
	.rmii_10 = RK3368_GMAC_RMII_CLK_2_5M | RK3368_GMAC_SPEED_10M,
	.rmii_100 = RK3368_GMAC_RMII_CLK_25M | RK3368_GMAC_SPEED_100M,
};

static int rk3368_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3368_reg_speed_data,
				RK3368_GRF_SOC_CON15, interface, speed);
}

static const struct rk_gmac_ops rk3368_ops = {
	.set_to_rgmii = rk3368_set_to_rgmii,
	.set_to_rmii = rk3368_set_to_rmii,
	.set_speed = rk3368_set_speed,
};

#define RK3399_GRF_SOC_CON5	0xc214
#define RK3399_GRF_SOC_CON6	0xc218

/* RK3399_GRF_SOC_CON5 */
#define RK3399_GMAC_PHY_INTF_SEL_RGMII	(GRF_BIT(9) | GRF_CLR_BIT(10) | \
					 GRF_CLR_BIT(11))
#define RK3399_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(9) | GRF_CLR_BIT(10) | \
					 GRF_BIT(11))
#define RK3399_GMAC_FLOW_CTRL		GRF_BIT(8)
#define RK3399_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(8)
#define RK3399_GMAC_SPEED_10M		GRF_CLR_BIT(7)
#define RK3399_GMAC_SPEED_100M		GRF_BIT(7)
#define RK3399_GMAC_RMII_CLK_25M	GRF_BIT(3)
#define RK3399_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(3)
#define RK3399_GMAC_CLK_125M		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5))
#define RK3399_GMAC_CLK_25M		(GRF_BIT(4) | GRF_BIT(5))
#define RK3399_GMAC_CLK_2_5M		(GRF_CLR_BIT(4) | GRF_BIT(5))
#define RK3399_GMAC_RMII_MODE		GRF_BIT(6)
#define RK3399_GMAC_RMII_MODE_CLR	GRF_CLR_BIT(6)

/* RK3399_GRF_SOC_CON6 */
#define RK3399_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(7)
#define RK3399_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(7)
#define RK3399_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3399_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3399_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 8)
#define RK3399_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void rk3399_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3399_GRF_SOC_CON5,
		     RK3399_GMAC_PHY_INTF_SEL_RGMII |
		     RK3399_GMAC_RMII_MODE_CLR);
	regmap_write(bsp_priv->grf, RK3399_GRF_SOC_CON6,
		     DELAY_ENABLE(RK3399, tx_delay, rx_delay) |
		     RK3399_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3399_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3399_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RK3399_GRF_SOC_CON5,
		     RK3399_GMAC_PHY_INTF_SEL_RMII | RK3399_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3399_reg_speed_data = {
	.rgmii_10 = RK3399_GMAC_CLK_2_5M,
	.rgmii_100 = RK3399_GMAC_CLK_25M,
	.rgmii_1000 = RK3399_GMAC_CLK_125M,
	.rmii_10 = RK3399_GMAC_RMII_CLK_2_5M | RK3399_GMAC_SPEED_10M,
	.rmii_100 = RK3399_GMAC_RMII_CLK_25M | RK3399_GMAC_SPEED_100M,
};

static int rk3399_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rk3399_reg_speed_data,
				RK3399_GRF_SOC_CON5, interface, speed);
}

static const struct rk_gmac_ops rk3399_ops = {
	.set_to_rgmii = rk3399_set_to_rgmii,
	.set_to_rmii = rk3399_set_to_rmii,
	.set_speed = rk3399_set_speed,
};

#define RK3528_VO_GRF_GMAC_CON		0x0018
#define RK3528_VO_GRF_MACPHY_CON0	0x001c
#define RK3528_VO_GRF_MACPHY_CON1	0x0020
#define RK3528_VPU_GRF_GMAC_CON5	0x0018
#define RK3528_VPU_GRF_GMAC_CON6	0x001c

#define RK3528_GMAC_RXCLK_DLY_ENABLE	GRF_BIT(15)
#define RK3528_GMAC_RXCLK_DLY_DISABLE	GRF_CLR_BIT(15)
#define RK3528_GMAC_TXCLK_DLY_ENABLE	GRF_BIT(14)
#define RK3528_GMAC_TXCLK_DLY_DISABLE	GRF_CLR_BIT(14)

#define RK3528_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0xFF, 8)
#define RK3528_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0xFF, 0)

#define RK3528_GMAC0_PHY_INTF_SEL_RMII	GRF_BIT(1)
#define RK3528_GMAC1_PHY_INTF_SEL_RGMII	GRF_CLR_BIT(8)
#define RK3528_GMAC1_PHY_INTF_SEL_RMII	GRF_BIT(8)

#define RK3528_GMAC1_CLK_SELECT_CRU	GRF_CLR_BIT(12)
#define RK3528_GMAC1_CLK_SELECT_IO	GRF_BIT(12)

#define RK3528_GMAC0_CLK_RMII_DIV2	GRF_BIT(3)
#define RK3528_GMAC0_CLK_RMII_DIV20	GRF_CLR_BIT(3)
#define RK3528_GMAC1_CLK_RMII_DIV2	GRF_BIT(10)
#define RK3528_GMAC1_CLK_RMII_DIV20	GRF_CLR_BIT(10)

#define RK3528_GMAC1_CLK_RGMII_DIV1	(GRF_CLR_BIT(11) | GRF_CLR_BIT(10))
#define RK3528_GMAC1_CLK_RGMII_DIV5	(GRF_BIT(11) | GRF_BIT(10))
#define RK3528_GMAC1_CLK_RGMII_DIV50	(GRF_BIT(11) | GRF_CLR_BIT(10))

#define RK3528_GMAC0_CLK_RMII_GATE	GRF_BIT(2)
#define RK3528_GMAC0_CLK_RMII_NOGATE	GRF_CLR_BIT(2)
#define RK3528_GMAC1_CLK_RMII_GATE	GRF_BIT(9)
#define RK3528_GMAC1_CLK_RMII_NOGATE	GRF_CLR_BIT(9)

static void rk3528_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RK3528_VPU_GRF_GMAC_CON5,
		     RK3528_GMAC1_PHY_INTF_SEL_RGMII);

	regmap_write(bsp_priv->grf, RK3528_VPU_GRF_GMAC_CON5,
		     DELAY_ENABLE(RK3528, tx_delay, rx_delay));

	regmap_write(bsp_priv->grf, RK3528_VPU_GRF_GMAC_CON6,
		     RK3528_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3528_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3528_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	if (bsp_priv->id == 1)
		regmap_write(bsp_priv->grf, RK3528_VPU_GRF_GMAC_CON5,
			     RK3528_GMAC1_PHY_INTF_SEL_RMII);
	else
		regmap_write(bsp_priv->grf, RK3528_VO_GRF_GMAC_CON,
			     RK3528_GMAC0_PHY_INTF_SEL_RMII |
			     RK3528_GMAC0_CLK_RMII_DIV2);
}

static const struct rk_reg_speed_data rk3528_gmac0_reg_speed_data = {
	.rmii_10 = RK3528_GMAC0_CLK_RMII_DIV20,
	.rmii_100 = RK3528_GMAC0_CLK_RMII_DIV2,
};

static const struct rk_reg_speed_data rk3528_gmac1_reg_speed_data = {
	.rgmii_10 = RK3528_GMAC1_CLK_RGMII_DIV50,
	.rgmii_100 = RK3528_GMAC1_CLK_RGMII_DIV5,
	.rgmii_1000 = RK3528_GMAC1_CLK_RGMII_DIV1,
	.rmii_10 = RK3528_GMAC1_CLK_RMII_DIV20,
	.rmii_100 = RK3528_GMAC1_CLK_RMII_DIV2,
};

static int rk3528_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	const struct rk_reg_speed_data *rsd;
	unsigned int reg;

	if (bsp_priv->id == 1) {
		rsd = &rk3528_gmac1_reg_speed_data;
		reg = RK3528_VPU_GRF_GMAC_CON5;
	} else {
		rsd = &rk3528_gmac0_reg_speed_data;
		reg = RK3528_VO_GRF_GMAC_CON;
	}

	return rk_set_reg_speed(bsp_priv, rsd, reg, interface, speed);
}

static void rk3528_set_clock_selection(struct rk_priv_data *bsp_priv,
				       bool input, bool enable)
{
	unsigned int val;

	if (bsp_priv->id == 1) {
		val = input ? RK3528_GMAC1_CLK_SELECT_IO :
			      RK3528_GMAC1_CLK_SELECT_CRU;
		val |= enable ? RK3528_GMAC1_CLK_RMII_NOGATE :
				RK3528_GMAC1_CLK_RMII_GATE;
		regmap_write(bsp_priv->grf, RK3528_VPU_GRF_GMAC_CON5, val);
	} else {
		val = enable ? RK3528_GMAC0_CLK_RMII_NOGATE :
			       RK3528_GMAC0_CLK_RMII_GATE;
		regmap_write(bsp_priv->grf, RK3528_VO_GRF_GMAC_CON, val);
	}
}

static void rk3528_integrated_phy_powerup(struct rk_priv_data *bsp_priv)
{
	rk_gmac_integrated_fephy_powerup(bsp_priv, RK3528_VO_GRF_MACPHY_CON0);
}

static void rk3528_integrated_phy_powerdown(struct rk_priv_data *bsp_priv)
{
	rk_gmac_integrated_fephy_powerdown(bsp_priv, RK3528_VO_GRF_MACPHY_CON0);
}

static const struct rk_gmac_ops rk3528_ops = {
	.set_to_rgmii = rk3528_set_to_rgmii,
	.set_to_rmii = rk3528_set_to_rmii,
	.set_speed = rk3528_set_speed,
	.set_clock_selection = rk3528_set_clock_selection,
	.integrated_phy_powerup = rk3528_integrated_phy_powerup,
	.integrated_phy_powerdown = rk3528_integrated_phy_powerdown,
	.regs_valid = true,
	.regs = {
		0xffbd0000, /* gmac0 */
		0xffbe0000, /* gmac1 */
		0x0, /* sentinel */
	},
};

#define RK3568_GRF_GMAC0_CON0		0x0380
#define RK3568_GRF_GMAC0_CON1		0x0384
#define RK3568_GRF_GMAC1_CON0		0x0388
#define RK3568_GRF_GMAC1_CON1		0x038c

/* RK3568_GRF_GMAC0_CON1 && RK3568_GRF_GMAC1_CON1 */
#define RK3568_GMAC_PHY_INTF_SEL_RGMII	\
		(GRF_BIT(4) | GRF_CLR_BIT(5) | GRF_CLR_BIT(6))
#define RK3568_GMAC_PHY_INTF_SEL_RMII	\
		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5) | GRF_BIT(6))
#define RK3568_GMAC_FLOW_CTRL			GRF_BIT(3)
#define RK3568_GMAC_FLOW_CTRL_CLR		GRF_CLR_BIT(3)
#define RK3568_GMAC_RXCLK_DLY_ENABLE		GRF_BIT(1)
#define RK3568_GMAC_RXCLK_DLY_DISABLE		GRF_CLR_BIT(1)
#define RK3568_GMAC_TXCLK_DLY_ENABLE		GRF_BIT(0)
#define RK3568_GMAC_TXCLK_DLY_DISABLE		GRF_CLR_BIT(0)

/* RK3568_GRF_GMAC0_CON0 && RK3568_GRF_GMAC1_CON0 */
#define RK3568_GMAC_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 8)
#define RK3568_GMAC_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void rk3568_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	u32 con0, con1;

	con0 = (bsp_priv->id == 1) ? RK3568_GRF_GMAC1_CON0 :
				     RK3568_GRF_GMAC0_CON0;
	con1 = (bsp_priv->id == 1) ? RK3568_GRF_GMAC1_CON1 :
				     RK3568_GRF_GMAC0_CON1;

	regmap_write(bsp_priv->grf, con0,
		     RK3568_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3568_GMAC_CLK_TX_DL_CFG(tx_delay));

	regmap_write(bsp_priv->grf, con1,
		     RK3568_GMAC_PHY_INTF_SEL_RGMII |
		     RK3568_GMAC_RXCLK_DLY_ENABLE |
		     RK3568_GMAC_TXCLK_DLY_ENABLE);
}

static void rk3568_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	u32 con1;

	con1 = (bsp_priv->id == 1) ? RK3568_GRF_GMAC1_CON1 :
				     RK3568_GRF_GMAC0_CON1;
	regmap_write(bsp_priv->grf, con1, RK3568_GMAC_PHY_INTF_SEL_RMII);
}

static const struct rk_gmac_ops rk3568_ops = {
	.set_to_rgmii = rk3568_set_to_rgmii,
	.set_to_rmii = rk3568_set_to_rmii,
	.set_speed = rk_set_clk_mac_speed,
	.regs_valid = true,
	.regs = {
		0xfe2a0000, /* gmac0 */
		0xfe010000, /* gmac1 */
		0x0, /* sentinel */
	},
};

/* VCCIO0_1_3_IOC */
#define RK3576_VCCIO0_1_3_IOC_CON2		0X6408
#define RK3576_VCCIO0_1_3_IOC_CON3		0X640c
#define RK3576_VCCIO0_1_3_IOC_CON4		0X6410
#define RK3576_VCCIO0_1_3_IOC_CON5		0X6414

#define RK3576_GMAC_RXCLK_DLY_ENABLE		GRF_BIT(15)
#define RK3576_GMAC_RXCLK_DLY_DISABLE		GRF_CLR_BIT(15)
#define RK3576_GMAC_TXCLK_DLY_ENABLE		GRF_BIT(7)
#define RK3576_GMAC_TXCLK_DLY_DISABLE		GRF_CLR_BIT(7)

#define RK3576_GMAC_CLK_RX_DL_CFG(val)		HIWORD_UPDATE(val, 0x7F, 8)
#define RK3576_GMAC_CLK_TX_DL_CFG(val)		HIWORD_UPDATE(val, 0x7F, 0)

/* SDGMAC_GRF */
#define RK3576_GRF_GMAC_CON0			0X0020
#define RK3576_GRF_GMAC_CON1			0X0024

#define RK3576_GMAC_RMII_MODE			GRF_BIT(3)
#define RK3576_GMAC_RGMII_MODE			GRF_CLR_BIT(3)

#define RK3576_GMAC_CLK_SELECT_IO		GRF_BIT(7)
#define RK3576_GMAC_CLK_SELECT_CRU		GRF_CLR_BIT(7)

#define RK3576_GMAC_CLK_RMII_DIV2		GRF_BIT(5)
#define RK3576_GMAC_CLK_RMII_DIV20		GRF_CLR_BIT(5)

#define RK3576_GMAC_CLK_RGMII_DIV1		\
			(GRF_CLR_BIT(6) | GRF_CLR_BIT(5))
#define RK3576_GMAC_CLK_RGMII_DIV5		\
			(GRF_BIT(6) | GRF_BIT(5))
#define RK3576_GMAC_CLK_RGMII_DIV50		\
			(GRF_BIT(6) | GRF_CLR_BIT(5))

#define RK3576_GMAC_CLK_RMII_GATE		GRF_BIT(4)
#define RK3576_GMAC_CLK_RMII_NOGATE		GRF_CLR_BIT(4)

static void rk3576_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	unsigned int offset_con;

	offset_con = bsp_priv->id == 1 ? RK3576_GRF_GMAC_CON1 :
					 RK3576_GRF_GMAC_CON0;

	regmap_write(bsp_priv->grf, offset_con, RK3576_GMAC_RGMII_MODE);

	offset_con = bsp_priv->id == 1 ? RK3576_VCCIO0_1_3_IOC_CON4 :
					 RK3576_VCCIO0_1_3_IOC_CON2;

	/* m0 && m1 delay enabled */
	regmap_write(bsp_priv->php_grf, offset_con,
		     DELAY_ENABLE(RK3576, tx_delay, rx_delay));
	regmap_write(bsp_priv->php_grf, offset_con + 0x4,
		     DELAY_ENABLE(RK3576, tx_delay, rx_delay));

	/* m0 && m1 delay value */
	regmap_write(bsp_priv->php_grf, offset_con,
		     RK3576_GMAC_CLK_TX_DL_CFG(tx_delay) |
		     RK3576_GMAC_CLK_RX_DL_CFG(rx_delay));
	regmap_write(bsp_priv->php_grf, offset_con + 0x4,
		     RK3576_GMAC_CLK_TX_DL_CFG(tx_delay) |
		     RK3576_GMAC_CLK_RX_DL_CFG(rx_delay));
}

static void rk3576_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	unsigned int offset_con;

	offset_con = bsp_priv->id == 1 ? RK3576_GRF_GMAC_CON1 :
					 RK3576_GRF_GMAC_CON0;

	regmap_write(bsp_priv->grf, offset_con, RK3576_GMAC_RMII_MODE);
}

static const struct rk_reg_speed_data rk3578_reg_speed_data = {
	.rgmii_10 = RK3576_GMAC_CLK_RGMII_DIV50,
	.rgmii_100 = RK3576_GMAC_CLK_RGMII_DIV5,
	.rgmii_1000 = RK3576_GMAC_CLK_RGMII_DIV1,
	.rmii_10 = RK3576_GMAC_CLK_RMII_DIV20,
	.rmii_100 = RK3576_GMAC_CLK_RMII_DIV2,
};

static int rk3576_set_gmac_speed(struct rk_priv_data *bsp_priv,
				 phy_interface_t interface, int speed)
{
	unsigned int offset_con;

	offset_con = bsp_priv->id == 1 ? RK3576_GRF_GMAC_CON1 :
					 RK3576_GRF_GMAC_CON0;

	return rk_set_reg_speed(bsp_priv, &rk3578_reg_speed_data, offset_con,
				interface, speed);
}

static void rk3576_set_clock_selection(struct rk_priv_data *bsp_priv, bool input,
				       bool enable)
{
	unsigned int val = input ? RK3576_GMAC_CLK_SELECT_IO :
				   RK3576_GMAC_CLK_SELECT_CRU;
	unsigned int offset_con;

	val |= enable ? RK3576_GMAC_CLK_RMII_NOGATE :
			RK3576_GMAC_CLK_RMII_GATE;

	offset_con = bsp_priv->id == 1 ? RK3576_GRF_GMAC_CON1 :
					 RK3576_GRF_GMAC_CON0;

	regmap_write(bsp_priv->grf, offset_con, val);
}

static const struct rk_gmac_ops rk3576_ops = {
	.set_to_rgmii = rk3576_set_to_rgmii,
	.set_to_rmii = rk3576_set_to_rmii,
	.set_speed = rk3576_set_gmac_speed,
	.set_clock_selection = rk3576_set_clock_selection,
	.php_grf_required = true,
	.regs_valid = true,
	.regs = {
		0x2a220000, /* gmac0 */
		0x2a230000, /* gmac1 */
		0x0, /* sentinel */
	},
};

/* sys_grf */
#define RK3588_GRF_GMAC_CON7			0X031c
#define RK3588_GRF_GMAC_CON8			0X0320
#define RK3588_GRF_GMAC_CON9			0X0324

#define RK3588_GMAC_RXCLK_DLY_ENABLE(id)	GRF_BIT(2 * (id) + 3)
#define RK3588_GMAC_RXCLK_DLY_DISABLE(id)	GRF_CLR_BIT(2 * (id) + 3)
#define RK3588_GMAC_TXCLK_DLY_ENABLE(id)	GRF_BIT(2 * (id) + 2)
#define RK3588_GMAC_TXCLK_DLY_DISABLE(id)	GRF_CLR_BIT(2 * (id) + 2)

#define RK3588_GMAC_CLK_RX_DL_CFG(val)		HIWORD_UPDATE(val, 0xFF, 8)
#define RK3588_GMAC_CLK_TX_DL_CFG(val)		HIWORD_UPDATE(val, 0xFF, 0)

/* php_grf */
#define RK3588_GRF_GMAC_CON0			0X0008
#define RK3588_GRF_CLK_CON1			0X0070

#define RK3588_GMAC_PHY_INTF_SEL_RGMII(id)	\
	(GRF_BIT(3 + (id) * 6) | GRF_CLR_BIT(4 + (id) * 6) | GRF_CLR_BIT(5 + (id) * 6))
#define RK3588_GMAC_PHY_INTF_SEL_RMII(id)	\
	(GRF_CLR_BIT(3 + (id) * 6) | GRF_CLR_BIT(4 + (id) * 6) | GRF_BIT(5 + (id) * 6))

#define RK3588_GMAC_CLK_RMII_MODE(id)		GRF_BIT(5 * (id))
#define RK3588_GMAC_CLK_RGMII_MODE(id)		GRF_CLR_BIT(5 * (id))

#define RK3588_GMAC_CLK_SELECT_CRU(id)		GRF_BIT(5 * (id) + 4)
#define RK3588_GMAC_CLK_SELECT_IO(id)		GRF_CLR_BIT(5 * (id) + 4)

#define RK3588_GMA_CLK_RMII_DIV2(id)		GRF_BIT(5 * (id) + 2)
#define RK3588_GMA_CLK_RMII_DIV20(id)		GRF_CLR_BIT(5 * (id) + 2)

#define RK3588_GMAC_CLK_RGMII_DIV1(id)		\
			(GRF_CLR_BIT(5 * (id) + 2) | GRF_CLR_BIT(5 * (id) + 3))
#define RK3588_GMAC_CLK_RGMII_DIV5(id)		\
			(GRF_BIT(5 * (id) + 2) | GRF_BIT(5 * (id) + 3))
#define RK3588_GMAC_CLK_RGMII_DIV50(id)		\
			(GRF_CLR_BIT(5 * (id) + 2) | GRF_BIT(5 * (id) + 3))

#define RK3588_GMAC_CLK_RMII_GATE(id)		GRF_BIT(5 * (id) + 1)
#define RK3588_GMAC_CLK_RMII_NOGATE(id)		GRF_CLR_BIT(5 * (id) + 1)

static void rk3588_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	u32 offset_con, id = bsp_priv->id;

	offset_con = bsp_priv->id == 1 ? RK3588_GRF_GMAC_CON9 :
					 RK3588_GRF_GMAC_CON8;

	regmap_write(bsp_priv->php_grf, RK3588_GRF_GMAC_CON0,
		     RK3588_GMAC_PHY_INTF_SEL_RGMII(id));

	regmap_write(bsp_priv->php_grf, RK3588_GRF_CLK_CON1,
		     RK3588_GMAC_CLK_RGMII_MODE(id));

	regmap_write(bsp_priv->grf, RK3588_GRF_GMAC_CON7,
		     RK3588_GMAC_RXCLK_DLY_ENABLE(id) |
		     RK3588_GMAC_TXCLK_DLY_ENABLE(id));

	regmap_write(bsp_priv->grf, offset_con,
		     RK3588_GMAC_CLK_RX_DL_CFG(rx_delay) |
		     RK3588_GMAC_CLK_TX_DL_CFG(tx_delay));
}

static void rk3588_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->php_grf, RK3588_GRF_GMAC_CON0,
		     RK3588_GMAC_PHY_INTF_SEL_RMII(bsp_priv->id));

	regmap_write(bsp_priv->php_grf, RK3588_GRF_CLK_CON1,
		     RK3588_GMAC_CLK_RMII_MODE(bsp_priv->id));
}

static int rk3588_set_gmac_speed(struct rk_priv_data *bsp_priv,
				 phy_interface_t interface, int speed)
{
	unsigned int val = 0, id = bsp_priv->id;

	switch (speed) {
	case 10:
		if (interface == PHY_INTERFACE_MODE_RMII)
			val = RK3588_GMA_CLK_RMII_DIV20(id);
		else
			val = RK3588_GMAC_CLK_RGMII_DIV50(id);
		break;
	case 100:
		if (interface == PHY_INTERFACE_MODE_RMII)
			val = RK3588_GMA_CLK_RMII_DIV2(id);
		else
			val = RK3588_GMAC_CLK_RGMII_DIV5(id);
		break;
	case 1000:
		if (interface != PHY_INTERFACE_MODE_RMII)
			val = RK3588_GMAC_CLK_RGMII_DIV1(id);
		else
			goto err;
		break;
	default:
		goto err;
	}

	regmap_write(bsp_priv->php_grf, RK3588_GRF_CLK_CON1, val);

	return 0;
err:
	return -EINVAL;
}

static void rk3588_set_clock_selection(struct rk_priv_data *bsp_priv, bool input,
				       bool enable)
{
	unsigned int val = input ? RK3588_GMAC_CLK_SELECT_IO(bsp_priv->id) :
				   RK3588_GMAC_CLK_SELECT_CRU(bsp_priv->id);

	val |= enable ? RK3588_GMAC_CLK_RMII_NOGATE(bsp_priv->id) :
			RK3588_GMAC_CLK_RMII_GATE(bsp_priv->id);

	regmap_write(bsp_priv->php_grf, RK3588_GRF_CLK_CON1, val);
}

static const struct rk_gmac_ops rk3588_ops = {
	.set_to_rgmii = rk3588_set_to_rgmii,
	.set_to_rmii = rk3588_set_to_rmii,
	.set_speed = rk3588_set_gmac_speed,
	.set_clock_selection = rk3588_set_clock_selection,
	.php_grf_required = true,
	.regs_valid = true,
	.regs = {
		0xfe1b0000, /* gmac0 */
		0xfe1c0000, /* gmac1 */
		0x0, /* sentinel */
	},
};

#define RV1108_GRF_GMAC_CON0		0X0900

/* RV1108_GRF_GMAC_CON0 */
#define RV1108_GMAC_PHY_INTF_SEL_RMII	(GRF_CLR_BIT(4) | GRF_CLR_BIT(5) | \
					GRF_BIT(6))
#define RV1108_GMAC_FLOW_CTRL		GRF_BIT(3)
#define RV1108_GMAC_FLOW_CTRL_CLR	GRF_CLR_BIT(3)
#define RV1108_GMAC_SPEED_10M		GRF_CLR_BIT(2)
#define RV1108_GMAC_SPEED_100M		GRF_BIT(2)
#define RV1108_GMAC_RMII_CLK_25M	GRF_BIT(7)
#define RV1108_GMAC_RMII_CLK_2_5M	GRF_CLR_BIT(7)

static void rv1108_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RV1108_GRF_GMAC_CON0,
		     RV1108_GMAC_PHY_INTF_SEL_RMII);
}

static const struct rk_reg_speed_data rv1108_reg_speed_data = {
	.rmii_10 = RV1108_GMAC_RMII_CLK_2_5M | RV1108_GMAC_SPEED_10M,
	.rmii_100 = RV1108_GMAC_RMII_CLK_25M | RV1108_GMAC_SPEED_100M,
};

static int rv1108_set_speed(struct rk_priv_data *bsp_priv,
			    phy_interface_t interface, int speed)
{
	return rk_set_reg_speed(bsp_priv, &rv1108_reg_speed_data,
				RV1108_GRF_GMAC_CON0, interface, speed);
}

static const struct rk_gmac_ops rv1108_ops = {
	.set_to_rmii = rv1108_set_to_rmii,
	.set_speed = rv1108_set_speed,
};

#define RV1126_GRF_GMAC_CON0		0X0070
#define RV1126_GRF_GMAC_CON1		0X0074
#define RV1126_GRF_GMAC_CON2		0X0078

/* RV1126_GRF_GMAC_CON0 */
#define RV1126_GMAC_PHY_INTF_SEL_RGMII	\
		(GRF_BIT(4) | GRF_CLR_BIT(5) | GRF_CLR_BIT(6))
#define RV1126_GMAC_PHY_INTF_SEL_RMII	\
		(GRF_CLR_BIT(4) | GRF_CLR_BIT(5) | GRF_BIT(6))
#define RV1126_GMAC_FLOW_CTRL			GRF_BIT(7)
#define RV1126_GMAC_FLOW_CTRL_CLR		GRF_CLR_BIT(7)
#define RV1126_GMAC_M0_RXCLK_DLY_ENABLE		GRF_BIT(1)
#define RV1126_GMAC_M0_RXCLK_DLY_DISABLE	GRF_CLR_BIT(1)
#define RV1126_GMAC_M0_TXCLK_DLY_ENABLE		GRF_BIT(0)
#define RV1126_GMAC_M0_TXCLK_DLY_DISABLE	GRF_CLR_BIT(0)
#define RV1126_GMAC_M1_RXCLK_DLY_ENABLE		GRF_BIT(3)
#define RV1126_GMAC_M1_RXCLK_DLY_DISABLE	GRF_CLR_BIT(3)
#define RV1126_GMAC_M1_TXCLK_DLY_ENABLE		GRF_BIT(2)
#define RV1126_GMAC_M1_TXCLK_DLY_DISABLE	GRF_CLR_BIT(2)

/* RV1126_GRF_GMAC_CON1 */
#define RV1126_GMAC_M0_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 8)
#define RV1126_GMAC_M0_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)
/* RV1126_GRF_GMAC_CON2 */
#define RV1126_GMAC_M1_CLK_RX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 8)
#define RV1126_GMAC_M1_CLK_TX_DL_CFG(val)	HIWORD_UPDATE(val, 0x7F, 0)

static void rv1126_set_to_rgmii(struct rk_priv_data *bsp_priv,
				int tx_delay, int rx_delay)
{
	regmap_write(bsp_priv->grf, RV1126_GRF_GMAC_CON0,
		     RV1126_GMAC_PHY_INTF_SEL_RGMII |
		     RV1126_GMAC_M0_RXCLK_DLY_ENABLE |
		     RV1126_GMAC_M0_TXCLK_DLY_ENABLE |
		     RV1126_GMAC_M1_RXCLK_DLY_ENABLE |
		     RV1126_GMAC_M1_TXCLK_DLY_ENABLE);

	regmap_write(bsp_priv->grf, RV1126_GRF_GMAC_CON1,
		     RV1126_GMAC_M0_CLK_RX_DL_CFG(rx_delay) |
		     RV1126_GMAC_M0_CLK_TX_DL_CFG(tx_delay));

	regmap_write(bsp_priv->grf, RV1126_GRF_GMAC_CON2,
		     RV1126_GMAC_M1_CLK_RX_DL_CFG(rx_delay) |
		     RV1126_GMAC_M1_CLK_TX_DL_CFG(tx_delay));
}

static void rv1126_set_to_rmii(struct rk_priv_data *bsp_priv)
{
	regmap_write(bsp_priv->grf, RV1126_GRF_GMAC_CON0,
		     RV1126_GMAC_PHY_INTF_SEL_RMII);
}

static const struct rk_gmac_ops rv1126_ops = {
	.set_to_rgmii = rv1126_set_to_rgmii,
	.set_to_rmii = rv1126_set_to_rmii,
	.set_speed = rk_set_clk_mac_speed,
};

static int rk_gmac_clk_init(struct plat_stmmacenet_data *plat)
{
	struct rk_priv_data *bsp_priv = plat->bsp_priv;
	int phy_iface = bsp_priv->phy_iface;
	struct device *dev = bsp_priv->dev;
	int i, j, ret;

	bsp_priv->clk_enabled = false;

	bsp_priv->num_clks = ARRAY_SIZE(rk_clocks);
	if (phy_iface == PHY_INTERFACE_MODE_RMII)
		bsp_priv->num_clks += ARRAY_SIZE(rk_rmii_clocks);

	bsp_priv->clks = devm_kcalloc(dev, bsp_priv->num_clks,
				      sizeof(*bsp_priv->clks), GFP_KERNEL);
	if (!bsp_priv->clks)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(rk_clocks); i++)
		bsp_priv->clks[i].id = rk_clocks[i];

	if (phy_iface == PHY_INTERFACE_MODE_RMII) {
		for (j = 0; j < ARRAY_SIZE(rk_rmii_clocks); j++)
			bsp_priv->clks[i++].id = rk_rmii_clocks[j];
	}

	ret = devm_clk_bulk_get_optional(dev, bsp_priv->num_clks,
					 bsp_priv->clks);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get clocks\n");

	if (bsp_priv->clock_input) {
		dev_info(dev, "clock input from PHY\n");
	} else if (phy_iface == PHY_INTERFACE_MODE_RMII) {
		clk_set_rate(plat->stmmac_clk, 50000000);
	}

	if (plat->phy_node && bsp_priv->integrated_phy) {
		bsp_priv->clk_phy = of_clk_get(plat->phy_node, 0);
		ret = PTR_ERR_OR_ZERO(bsp_priv->clk_phy);
		if (ret)
			return dev_err_probe(dev, ret, "Cannot get PHY clock\n");
		clk_set_rate(bsp_priv->clk_phy, 50000000);
	}

	return 0;
}

static int gmac_clk_enable(struct rk_priv_data *bsp_priv, bool enable)
{
	int ret;

	if (enable) {
		if (!bsp_priv->clk_enabled) {
			ret = clk_bulk_prepare_enable(bsp_priv->num_clks,
						      bsp_priv->clks);
			if (ret)
				return ret;

			ret = clk_prepare_enable(bsp_priv->clk_phy);
			if (ret)
				return ret;

			if (bsp_priv->ops && bsp_priv->ops->set_clock_selection)
				bsp_priv->ops->set_clock_selection(bsp_priv,
					       bsp_priv->clock_input, true);

			mdelay(5);
			bsp_priv->clk_enabled = true;
		}
	} else {
		if (bsp_priv->clk_enabled) {
			if (bsp_priv->ops && bsp_priv->ops->set_clock_selection) {
				bsp_priv->ops->set_clock_selection(bsp_priv,
					      bsp_priv->clock_input, false);
			}

			clk_bulk_disable_unprepare(bsp_priv->num_clks,
						   bsp_priv->clks);
			clk_disable_unprepare(bsp_priv->clk_phy);

			bsp_priv->clk_enabled = false;
		}
	}

	return 0;
}

static int phy_power_on(struct rk_priv_data *bsp_priv, bool enable)
{
	struct regulator *ldo = bsp_priv->regulator;
	struct device *dev = bsp_priv->dev;
	int ret;

	if (enable) {
		ret = regulator_enable(ldo);
		if (ret)
			dev_err(dev, "fail to enable phy-supply\n");
	} else {
		ret = regulator_disable(ldo);
		if (ret)
			dev_err(dev, "fail to disable phy-supply\n");
	}

	return 0;
}

static struct rk_priv_data *rk_gmac_setup(struct platform_device *pdev,
					  struct plat_stmmacenet_data *plat,
					  const struct rk_gmac_ops *ops)
{
	struct rk_priv_data *bsp_priv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;
	const char *strings = NULL;
	int value;

	bsp_priv = devm_kzalloc(dev, sizeof(*bsp_priv), GFP_KERNEL);
	if (!bsp_priv)
		return ERR_PTR(-ENOMEM);

	bsp_priv->phy_iface = plat->phy_interface;
	bsp_priv->ops = ops;

	/* Some SoCs have multiple MAC controllers, which need
	 * to be distinguished.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res && ops->regs_valid) {
		int i = 0;

		while (ops->regs[i]) {
			if (ops->regs[i] == res->start) {
				bsp_priv->id = i;
				break;
			}
			i++;
		}
	}

	bsp_priv->regulator = devm_regulator_get(dev, "phy");
	if (IS_ERR(bsp_priv->regulator)) {
		ret = PTR_ERR(bsp_priv->regulator);
		dev_err_probe(dev, ret, "failed to get phy regulator\n");
		return ERR_PTR(ret);
	}

	ret = of_property_read_string(dev->of_node, "clock_in_out", &strings);
	if (ret) {
		dev_err(dev, "Can not read property: clock_in_out.\n");
		bsp_priv->clock_input = true;
	} else {
		dev_info(dev, "clock input or output? (%s).\n",
			 strings);
		if (!strcmp(strings, "input"))
			bsp_priv->clock_input = true;
		else
			bsp_priv->clock_input = false;
	}

	ret = of_property_read_u32(dev->of_node, "tx_delay", &value);
	if (ret) {
		bsp_priv->tx_delay = 0x30;
		dev_err(dev, "Can not read property: tx_delay.");
		dev_err(dev, "set tx_delay to 0x%x\n",
			bsp_priv->tx_delay);
	} else {
		dev_info(dev, "TX delay(0x%x).\n", value);
		bsp_priv->tx_delay = value;
	}

	ret = of_property_read_u32(dev->of_node, "rx_delay", &value);
	if (ret) {
		bsp_priv->rx_delay = 0x10;
		dev_err(dev, "Can not read property: rx_delay.");
		dev_err(dev, "set rx_delay to 0x%x\n",
			bsp_priv->rx_delay);
	} else {
		dev_info(dev, "RX delay(0x%x).\n", value);
		bsp_priv->rx_delay = value;
	}

	bsp_priv->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
							"rockchip,grf");
	if (IS_ERR(bsp_priv->grf)) {
		dev_err_probe(dev, PTR_ERR(bsp_priv->grf),
			      "failed to lookup rockchip,grf\n");
		return ERR_CAST(bsp_priv->grf);
	}

	if (ops->php_grf_required) {
		bsp_priv->php_grf =
			syscon_regmap_lookup_by_phandle(dev->of_node,
							"rockchip,php-grf");
		if (IS_ERR(bsp_priv->php_grf)) {
			dev_err_probe(dev, PTR_ERR(bsp_priv->php_grf),
				      "failed to lookup rockchip,php-grf\n");
			return ERR_CAST(bsp_priv->php_grf);
		}
	}

	if (plat->phy_node) {
		bsp_priv->integrated_phy = of_property_read_bool(plat->phy_node,
								 "phy-is-integrated");
		if (bsp_priv->integrated_phy) {
			bsp_priv->phy_reset = of_reset_control_get(plat->phy_node, NULL);
			if (IS_ERR(bsp_priv->phy_reset)) {
				dev_err(&pdev->dev, "No PHY reset control found.\n");
				bsp_priv->phy_reset = NULL;
			}
		}
	}
	dev_info(dev, "integrated PHY? (%s).\n",
		 bsp_priv->integrated_phy ? "yes" : "no");

	bsp_priv->dev = dev;

	return bsp_priv;
}

static int rk_gmac_check_ops(struct rk_priv_data *bsp_priv)
{
	switch (bsp_priv->phy_iface) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		if (!bsp_priv->ops->set_to_rgmii)
			return -EINVAL;
		break;
	case PHY_INTERFACE_MODE_RMII:
		if (!bsp_priv->ops->set_to_rmii)
			return -EINVAL;
		break;
	default:
		dev_err(bsp_priv->dev,
			"unsupported interface %d", bsp_priv->phy_iface);
	}
	return 0;
}

static int rk_gmac_powerup(struct rk_priv_data *bsp_priv)
{
	struct device *dev = bsp_priv->dev;
	int ret;

	ret = rk_gmac_check_ops(bsp_priv);
	if (ret)
		return ret;

	ret = gmac_clk_enable(bsp_priv, true);
	if (ret)
		return ret;

	/*rmii or rgmii*/
	switch (bsp_priv->phy_iface) {
	case PHY_INTERFACE_MODE_RGMII:
		dev_info(dev, "init for RGMII\n");
		bsp_priv->ops->set_to_rgmii(bsp_priv, bsp_priv->tx_delay,
					    bsp_priv->rx_delay);
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		dev_info(dev, "init for RGMII_ID\n");
		bsp_priv->ops->set_to_rgmii(bsp_priv, 0, 0);
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		dev_info(dev, "init for RGMII_RXID\n");
		bsp_priv->ops->set_to_rgmii(bsp_priv, bsp_priv->tx_delay, 0);
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		dev_info(dev, "init for RGMII_TXID\n");
		bsp_priv->ops->set_to_rgmii(bsp_priv, 0, bsp_priv->rx_delay);
		break;
	case PHY_INTERFACE_MODE_RMII:
		dev_info(dev, "init for RMII\n");
		bsp_priv->ops->set_to_rmii(bsp_priv);
		break;
	default:
		dev_err(dev, "NO interface defined!\n");
	}

	ret = phy_power_on(bsp_priv, true);
	if (ret) {
		gmac_clk_enable(bsp_priv, false);
		return ret;
	}

	pm_runtime_get_sync(dev);

	if (bsp_priv->integrated_phy && bsp_priv->ops->integrated_phy_powerup)
		bsp_priv->ops->integrated_phy_powerup(bsp_priv);

	return 0;
}

static void rk_gmac_powerdown(struct rk_priv_data *gmac)
{
	if (gmac->integrated_phy && gmac->ops->integrated_phy_powerdown)
		gmac->ops->integrated_phy_powerdown(gmac);

	pm_runtime_put_sync(gmac->dev);

	phy_power_on(gmac, false);
	gmac_clk_enable(gmac, false);
}

static void rk_get_interfaces(struct stmmac_priv *priv, void *bsp_priv,
			      unsigned long *interfaces)
{
	struct rk_priv_data *rk = bsp_priv;

	if (rk->ops->set_to_rgmii)
		phy_interface_set_rgmii(interfaces);

	if (rk->ops->set_to_rmii)
		__set_bit(PHY_INTERFACE_MODE_RMII, interfaces);
}

static int rk_set_clk_tx_rate(void *bsp_priv_, struct clk *clk_tx_i,
			      phy_interface_t interface, int speed)
{
	struct rk_priv_data *bsp_priv = bsp_priv_;

	if (bsp_priv->ops->set_speed)
		return bsp_priv->ops->set_speed(bsp_priv, bsp_priv->phy_iface,
						speed);

	return -EINVAL;
}

static int rk_gmac_suspend(struct device *dev, void *bsp_priv_)
{
	struct rk_priv_data *bsp_priv = bsp_priv_;

	/* Keep the PHY up if we use Wake-on-Lan. */
	if (!device_may_wakeup(dev))
		rk_gmac_powerdown(bsp_priv);

	return 0;
}

static int rk_gmac_resume(struct device *dev, void *bsp_priv_)
{
	struct rk_priv_data *bsp_priv = bsp_priv_;

	/* The PHY was up for Wake-on-Lan. */
	if (!device_may_wakeup(dev))
		rk_gmac_powerup(bsp_priv);

	return 0;
}

/*
 * delay-scanning taken from Rockhip vendor-kernel.
 */

static void dwmac_rk_set_rgmii_delayline(struct stmmac_priv *priv,
				  int tx_delay, int rx_delay)
{
	struct rk_priv_data *bsp_priv = priv->plat->bsp_priv;

	if (bsp_priv->ops->set_to_rgmii) {
		bsp_priv->ops->set_to_rgmii(bsp_priv, tx_delay, rx_delay);
		bsp_priv->tx_delay = tx_delay;
		bsp_priv->rx_delay = rx_delay;
	}
}

static void dwmac_rk_get_rgmii_delayline(struct stmmac_priv *priv,
				  int *tx_delay, int *rx_delay)
{
	struct rk_priv_data *bsp_priv = priv->plat->bsp_priv;

	if (!bsp_priv->ops->set_to_rgmii)
		return;

	*tx_delay = bsp_priv->tx_delay;
	*rx_delay = bsp_priv->rx_delay;
}

static int dwmac_rk_get_phy_interface(struct stmmac_priv *priv)
{
	struct rk_priv_data *bsp_priv = priv->plat->bsp_priv;

	return bsp_priv->phy_iface;
}

enum {
	LOOPBACK_TYPE_GMAC = 1,
	LOOPBACK_TYPE_PHY
};

enum {
	LOOPBACK_SPEED10 = 10,
	LOOPBACK_SPEED100 = 100,
	LOOPBACK_SPEED1000 = 1000
};

struct dwmac_rk_packet_attrs {
	unsigned char src[6];
	unsigned char dst[6];
	u32 ip_src;
	u32 ip_dst;
	int tcp;
	int sport;
	int dport;
	int size;
};

struct dwmac_rk_hdr {
	__be32 version;
	__be64 magic;
	u32 id;
	int tx;
	int rx;
} __packed;

struct dwmac_rk_lb_priv {
	/* desc && buffer */
	struct dma_desc *dma_tx;
	dma_addr_t dma_tx_phy;
	struct sk_buff *tx_skbuff;
	dma_addr_t tx_skbuff_dma;
	unsigned int tx_skbuff_dma_len;

	struct dma_desc *dma_rx ____cacheline_aligned_in_smp;
	dma_addr_t dma_rx_phy;
	struct sk_buff *rx_skbuff;
	dma_addr_t rx_skbuff_dma;
	u32 rx_tail_addr;
	u32 tx_tail_addr;

	/* rx buffer size */
	unsigned int dma_buf_sz;
	unsigned int buf_sz;

	int type;
	int speed;
	struct dwmac_rk_packet_attrs *packet;

	unsigned int actual_size;
	int scan;
	int sysfs;
	u32 id;
	int tx;
	int rx;
	int final_tx;
	int final_rx;
	int max_delay;
};

#define RK_DMA_CONTROL_OSP		BIT(4)

#define RK_DMA_CHAN_BASE_ADDR	0x00001100
#define RK_DMA_CHAN_BASE_OFFSET	0x80
#define RK_DMA_CHANX_BASE_ADDR(x)	(RK_DMA_CHAN_BASE_ADDR + \
				((x) * RK_DMA_CHAN_BASE_OFFSET))
#define RK_DMA_CHAN_TX_CONTROL(x)	(RK_DMA_CHANX_BASE_ADDR(x) + 0x4)

#define RK_DMA_CHAN_STATUS(x)	(RK_DMA_CHANX_BASE_ADDR(x) + 0x60)

#define RK_DMA_CHAN_STATUS_ERI	BIT(11)
#define RK_DMA_CHAN_STATUS_ETI	BIT(10)

#define	STMMAC_ALIGN(x) __ALIGN_KERNEL(x, SMP_CACHE_BYTES)
#define MAX_DELAYLINE 0x7f
#define RK3588_MAX_DELAYLINE 0xc7
#define SCAN_STEP 0x5
#define SCAN_VALID_RANGE 0xA

#define DWMAC_RK_TEST_PKT_SIZE (sizeof(struct ethhdr) + sizeof(struct iphdr) + \
				sizeof(struct dwmac_rk_hdr))
#define DWMAC_RK_TEST_PKT_MAGIC 0xdeadcafecafedeadULL
#define DWMAC_RK_TEST_PKT_MAX_SIZE 1500

static __maybe_unused struct dwmac_rk_packet_attrs dwmac_rk_udp_attr = {
	.dst = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
	.tcp = 0,
	.size = 1024,
};

static __maybe_unused struct dwmac_rk_packet_attrs dwmac_rk_tcp_attr = {
	.dst = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
	.tcp = 1,
	.size = 1024,
};

static int dwmac_rk_enable_mac_loopback(struct stmmac_priv *priv, int speed,
					int addr, bool phy)
{
	struct rk_priv_data *bsp_priv = priv->plat->bsp_priv;
	u32 ctrl;
	int phy_val;

	ctrl = readl(priv->ioaddr + GMAC_CONTROL);
	ctrl &= ~priv->hw->link.speed_mask;
	ctrl |= GMAC_CONTROL_LM;

	if (phy)
		phy_val = mdiobus_read(priv->mii, addr, MII_BMCR);

	switch (speed) {
	case LOOPBACK_SPEED1000:
		ctrl |= priv->hw->link.speed1000;
		if (phy) {
			phy_val &= ~BMCR_SPEED100;
			phy_val |= BMCR_SPEED1000;
		}
		break;
	case LOOPBACK_SPEED100:
		ctrl |= priv->hw->link.speed100;
		if (phy) {
			phy_val &= ~BMCR_SPEED1000;
			phy_val |= BMCR_SPEED100;
		}
		break;
	case LOOPBACK_SPEED10:
		ctrl |= priv->hw->link.speed10;
		if (phy) {
			phy_val &= ~BMCR_SPEED1000;
			phy_val &= ~BMCR_SPEED100;
		}
		break;
	default:
		return -EPERM;
	}

	ctrl |= priv->hw->link.duplex;
	writel(ctrl, priv->ioaddr + GMAC_CONTROL);

	if (phy) {
		phy_val &= ~BMCR_PDOWN;
		phy_val &= ~BMCR_ANENABLE;
		phy_val &= ~BMCR_PDOWN;
		phy_val |= BMCR_FULLDPLX;
		mdiobus_write(priv->mii, addr, MII_BMCR, phy_val);
		phy_val = mdiobus_read(priv->mii, addr, MII_BMCR);
	}

	if (bsp_priv->ops->set_speed)
		return bsp_priv->ops->set_speed(bsp_priv, bsp_priv->phy_iface,
						speed);

	return 0;
}

static int dwmac_rk_disable_mac_loopback(struct stmmac_priv *priv, int addr)
{
	u32 ctrl;
	int phy_val;

	ctrl = readl(priv->ioaddr + GMAC_CONTROL);
	ctrl &= ~GMAC_CONTROL_LM;
	writel(ctrl, priv->ioaddr + GMAC_CONTROL);

	phy_val = mdiobus_read(priv->mii, addr, MII_BMCR);
	phy_val |= BMCR_ANENABLE;

	mdiobus_write(priv->mii, addr, MII_BMCR, phy_val);
	phy_val = mdiobus_read(priv->mii, addr, MII_BMCR);

	return 0;
}

static int dwmac_rk_set_mac_loopback(struct stmmac_priv *priv,
				     int speed, bool enable,
				     int addr, bool phy)
{
	if (enable)
		return dwmac_rk_enable_mac_loopback(priv, speed, addr, phy);
	else
		return dwmac_rk_disable_mac_loopback(priv, addr);
}

static int dwmac_rk_enable_phy_loopback(struct stmmac_priv *priv, int speed,
					int addr, bool phy)
{
	struct rk_priv_data *bsp_priv = priv->plat->bsp_priv;
	u32 ctrl;
	int val;

	ctrl = readl(priv->ioaddr + MAC_CTRL_REG);
	ctrl &= ~priv->hw->link.speed_mask;

	if (phy)
		val = mdiobus_read(priv->mii, addr, MII_BMCR);

	switch (speed) {
	case LOOPBACK_SPEED1000:
		ctrl |= priv->hw->link.speed1000;
		if (phy) {
			val &= ~BMCR_SPEED100;
			val |= BMCR_SPEED1000;
		}
		break;
	case LOOPBACK_SPEED100:
		ctrl |= priv->hw->link.speed100;
		if (phy) {
			val &= ~BMCR_SPEED1000;
			val |= BMCR_SPEED100;
		}
		break;
	case LOOPBACK_SPEED10:
		ctrl |= priv->hw->link.speed10;
		if (phy) {
			val &= ~BMCR_SPEED1000;
			val &= ~BMCR_SPEED100;
		}
		break;
	default:
		return -EPERM;
	}

	ctrl |= priv->hw->link.duplex;
	writel(ctrl, priv->ioaddr + MAC_CTRL_REG);

	if (phy) {
		val |= BMCR_FULLDPLX;
		val &= ~BMCR_PDOWN;
		val &= ~BMCR_ANENABLE;
		val |= BMCR_LOOPBACK;
		mdiobus_write(priv->mii, addr, MII_BMCR, val);
		val = mdiobus_read(priv->mii, addr, MII_BMCR);
	}

	if (bsp_priv->ops->set_speed)
		return bsp_priv->ops->set_speed(bsp_priv, bsp_priv->phy_iface,
						speed);

	return 0;
}

static int dwmac_rk_disable_phy_loopback(struct stmmac_priv *priv, int addr)
{
	int val;

	val = mdiobus_read(priv->mii, addr, MII_BMCR);
	val |= BMCR_ANENABLE;
	val &= ~BMCR_LOOPBACK;

	mdiobus_write(priv->mii, addr, MII_BMCR, val);
	val = mdiobus_read(priv->mii, addr, MII_BMCR);

	return 0;
}

static int dwmac_rk_set_phy_loopback(struct stmmac_priv *priv,
				     int speed, bool enable,
				     int addr, bool phy)
{
	if (enable)
		return dwmac_rk_enable_phy_loopback(priv, speed,
						     addr, phy);
	else
		return dwmac_rk_disable_phy_loopback(priv, addr);
}

static int dwmac_rk_set_loopback(struct stmmac_priv *priv,
				 int type, int speed, bool enable,
				 int addr, bool phy)
{
	int ret;

	switch (type) {
	case LOOPBACK_TYPE_PHY:
		ret = dwmac_rk_set_phy_loopback(priv, speed, enable, addr, phy);
		break;
	case LOOPBACK_TYPE_GMAC:
		ret = dwmac_rk_set_mac_loopback(priv, speed, enable, addr, phy);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	usleep_range(100000, 200000);
	return ret;
}

static inline void dwmac_rk_ether_addr_copy(u8 *dst, const u8 *src)
{
	u16 *a = (u16 *)dst;
	const u16 *b = (const u16 *)src;

	a[0] = b[0];
	a[1] = b[1];
	a[2] = b[2];
}

static void dwmac_rk_udp4_hwcsum(struct sk_buff *skb, __be32 src, __be32 dst)
{
	struct udphdr *uh = udp_hdr(skb);
	int offset = skb_transport_offset(skb);
	int len = skb->len - offset;

	skb->csum_start = skb_transport_header(skb) - skb->head;
	skb->csum_offset = offsetof(struct udphdr, check);
	uh->check = ~csum_tcpudp_magic(src, dst, len,
				       IPPROTO_UDP, 0);
}

static struct sk_buff *dwmac_rk_get_skb(struct stmmac_priv *priv,
					struct dwmac_rk_lb_priv *lb_priv)
{
	struct sk_buff *skb = NULL;
	struct udphdr *uhdr = NULL;
	struct tcphdr *thdr = NULL;
	struct dwmac_rk_hdr *shdr;
	struct ethhdr *ehdr;
	struct iphdr *ihdr;
	struct dwmac_rk_packet_attrs *attr;
	int iplen, size, nfrags;

	attr = lb_priv->packet;
	size = attr->size + DWMAC_RK_TEST_PKT_SIZE;
	if (attr->tcp)
		size += sizeof(struct tcphdr);
	else
		size += sizeof(struct udphdr);

	if (size >= DWMAC_RK_TEST_PKT_MAX_SIZE)
		return NULL;

	lb_priv->actual_size = size;

	skb = netdev_alloc_skb_ip_align(priv->dev, size);
	if (!skb)
		return NULL;

	skb_linearize(skb);
	nfrags = skb_shinfo(skb)->nr_frags;
	if (nfrags > 0) {
		pr_err("%s: TX nfrags is not zero\n", __func__);
		dev_kfree_skb(skb);
		return NULL;
	}

	ehdr = (struct ethhdr *)skb_push(skb, ETH_HLEN);
	skb_reset_mac_header(skb);

	skb_set_network_header(skb, skb->len);
	ihdr = (struct iphdr *)skb_put(skb, sizeof(*ihdr));

	skb_set_transport_header(skb, skb->len);
	if (attr->tcp)
		thdr = (struct tcphdr *)skb_put(skb, sizeof(*thdr));
	else
		uhdr = (struct udphdr *)skb_put(skb, sizeof(*uhdr));

	eth_zero_addr(ehdr->h_source);
	eth_zero_addr(ehdr->h_dest);

	dwmac_rk_ether_addr_copy(ehdr->h_source, priv->dev->dev_addr);
	dwmac_rk_ether_addr_copy(ehdr->h_dest, attr->dst);

	ehdr->h_proto = htons(ETH_P_IP);

	if (attr->tcp) {
		if (!thdr) {
			dev_kfree_skb(skb);
			return NULL;
		}

		thdr->source = htons(attr->sport);
		thdr->dest = htons(attr->dport);
		thdr->doff = sizeof(struct tcphdr) / 4;
		thdr->check = 0;
	} else {
		if (!uhdr) {
			dev_kfree_skb(skb);
			return NULL;
		}

		uhdr->source = htons(attr->sport);
		uhdr->dest = htons(attr->dport);
		uhdr->len = htons(sizeof(*shdr) + sizeof(*uhdr) + attr->size);
		uhdr->check = 0;
	}

	ihdr->ihl = 5;
	ihdr->ttl = 32;
	ihdr->version = 4;
	if (attr->tcp)
		ihdr->protocol = IPPROTO_TCP;
	else
		ihdr->protocol = IPPROTO_UDP;

	iplen = sizeof(*ihdr) + sizeof(*shdr) + attr->size;
	if (attr->tcp)
		iplen += sizeof(*thdr);
	else
		iplen += sizeof(*uhdr);

	ihdr->tot_len = htons(iplen);
	ihdr->frag_off = 0;
	ihdr->saddr = htonl(attr->ip_src);
	ihdr->daddr = htonl(attr->ip_dst);
	ihdr->tos = 0;
	ihdr->id = 0;
	ip_send_check(ihdr);

	shdr = (struct dwmac_rk_hdr *)skb_put(skb, sizeof(*shdr));
	shdr->version = 0;
	shdr->magic = cpu_to_be64(DWMAC_RK_TEST_PKT_MAGIC);
	shdr->id = lb_priv->id;
	shdr->tx = lb_priv->tx;
	shdr->rx = lb_priv->rx;

	if (attr->size) {
		skb_put(skb, attr->size);
		get_random_bytes((u8 *)shdr + sizeof(*shdr), attr->size);
	}

	skb->csum = 0;
	skb->ip_summed = CHECKSUM_PARTIAL;
	if (attr->tcp) {
		if (!thdr) {
			dev_kfree_skb(skb);
			return NULL;
		}

		thdr->check = ~tcp_v4_check(skb->len, ihdr->saddr,
					    ihdr->daddr, 0);
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct tcphdr, check);
	} else {
		dwmac_rk_udp4_hwcsum(skb, ihdr->saddr, ihdr->daddr);
	}

	skb->protocol = htons(ETH_P_IP);
	skb->pkt_type = PACKET_HOST;

	return skb;
}

static int dwmac_rk_loopback_validate(struct stmmac_priv *priv,
				      struct dwmac_rk_lb_priv *lb_priv,
				      struct sk_buff *skb)
{
	struct dwmac_rk_hdr *shdr;
	struct ethhdr *ehdr;
	struct udphdr *uhdr;
	struct tcphdr *thdr;
	struct iphdr *ihdr;
	int ret = -EAGAIN;

	if (skb->len >= DWMAC_RK_TEST_PKT_MAX_SIZE)
		goto out;

	if (lb_priv->actual_size != skb->len)
		goto out;

	ehdr = (struct ethhdr *)(skb->data);
	if (!ether_addr_equal(ehdr->h_dest, lb_priv->packet->dst))
		goto out;

	if (!ether_addr_equal(ehdr->h_source, priv->dev->dev_addr))
		goto out;

	ihdr = (struct iphdr *)(skb->data + ETH_HLEN);

	if (lb_priv->packet->tcp) {
		if (ihdr->protocol != IPPROTO_TCP)
			goto out;

		thdr = (struct tcphdr *)((u8 *)ihdr + 4 * ihdr->ihl);
		if (thdr->dest != htons(lb_priv->packet->dport))
			goto out;

		shdr = (struct dwmac_rk_hdr *)((u8 *)thdr + sizeof(*thdr));
	} else {
		if (ihdr->protocol != IPPROTO_UDP)
			goto out;

		uhdr = (struct udphdr *)((u8 *)ihdr + 4 * ihdr->ihl);
		if (uhdr->dest != htons(lb_priv->packet->dport))
			goto out;

		shdr = (struct dwmac_rk_hdr *)((u8 *)uhdr + sizeof(*uhdr));
	}

	if (shdr->magic != cpu_to_be64(DWMAC_RK_TEST_PKT_MAGIC))
		goto out;

	if (lb_priv->id != shdr->id)
		goto out;

	if (lb_priv->tx != shdr->tx || lb_priv->rx != shdr->rx)
		goto out;

	ret = 0;
out:
	return ret;
}

static inline int dwmac_rk_rx_fill(struct stmmac_priv *priv,
				   struct dwmac_rk_lb_priv *lb_priv)
{
	struct dma_desc *p;
	struct sk_buff *skb;

	p = lb_priv->dma_rx;
	if (likely(!lb_priv->rx_skbuff)) {
		skb = netdev_alloc_skb_ip_align(priv->dev, lb_priv->buf_sz);
		if (unlikely(!skb))
			return -ENOMEM;

		if (skb_linearize(skb)) {
			pr_err("%s: Rx skb linearize failed\n", __func__);
			lb_priv->rx_skbuff = NULL;
			dev_kfree_skb(skb);
			return -EPERM;
		}

		lb_priv->rx_skbuff = skb;
		lb_priv->rx_skbuff_dma =
		    dma_map_single(priv->device, skb->data, lb_priv->dma_buf_sz,
				   DMA_FROM_DEVICE);
		if (dma_mapping_error(priv->device,
				      lb_priv->rx_skbuff_dma)) {
			pr_err("%s: Rx dma map failed\n", __func__);
			lb_priv->rx_skbuff = NULL;
			dev_kfree_skb(skb);
			return -EFAULT;
		}

		stmmac_set_desc_addr(priv, p, lb_priv->rx_skbuff_dma);
		/* Fill DES3 in case of RING mode */
		if (lb_priv->dma_buf_sz == BUF_SIZE_16KiB)
			p->des3 = cpu_to_le32(le32_to_cpu(p->des2) + BUF_SIZE_8KiB);
	}

	wmb();
	stmmac_set_rx_owner(priv, p, priv->use_riwt);
	wmb();

	stmmac_set_rx_tail_ptr(priv, priv->ioaddr, lb_priv->rx_tail_addr, 0);

	return 0;
}

static void dwmac_rk_rx_clean(struct stmmac_priv *priv,
			      struct dwmac_rk_lb_priv *lb_priv)
{
	if (likely(lb_priv->rx_skbuff_dma)) {
		dma_unmap_single(priv->device,
				 lb_priv->rx_skbuff_dma,
				 lb_priv->dma_buf_sz, DMA_FROM_DEVICE);
		lb_priv->rx_skbuff_dma = 0;
	}

	if (likely(lb_priv->rx_skbuff)) {
		dev_consume_skb_any(lb_priv->rx_skbuff);
		lb_priv->rx_skbuff = NULL;
	}
}

static int dwmac_rk_rx_validate(struct stmmac_priv *priv,
				struct dwmac_rk_lb_priv *lb_priv)
{
	struct dma_desc *p;
	struct sk_buff *skb;
	int coe = priv->hw->rx_csum;
	unsigned int frame_len;

	p = lb_priv->dma_rx;
	skb = lb_priv->rx_skbuff;
	if (unlikely(!skb)) {
		pr_err("%s: Inconsistent Rx descriptor chain\n",
		       __func__);
		return -EINVAL;
	}

	frame_len = priv->hw->desc->get_rx_frame_len(p, coe);
	/*  check if frame_len fits the preallocated memory */
	if (frame_len > lb_priv->dma_buf_sz) {
		pr_err("%s: frame_len long: %d\n", __func__, frame_len);
		return -ENOMEM;
	}

	frame_len -= ETH_FCS_LEN;
	prefetch(skb->data - NET_IP_ALIGN);
	skb_put(skb, frame_len);
	dma_unmap_single(priv->device,
			 lb_priv->rx_skbuff_dma,
			 lb_priv->dma_buf_sz,
			 DMA_FROM_DEVICE);

	return dwmac_rk_loopback_validate(priv, lb_priv, skb);
}

static int dwmac_rk_get_desc_status(struct stmmac_priv *priv,
				    struct dwmac_rk_lb_priv *lb_priv)
{
	struct dma_desc *txp, *rxp;
	int tx_status, rx_status;

	txp = lb_priv->dma_tx;
	tx_status = priv->hw->desc->tx_status(&priv->xstats, txp,
					      priv->ioaddr);
	/* Check if the descriptor is owned by the DMA */
	if (unlikely(tx_status & tx_dma_own))
		return -EBUSY;

	rxp = lb_priv->dma_rx;
	/* read the status of the incoming frame */
	rx_status = priv->hw->desc->rx_status(&priv->xstats, rxp);

	if (unlikely(rx_status & dma_own))
		return -EBUSY;

	usleep_range(100, 150);

	return 0;
}

static void dwmac_rk_tx_clean(struct stmmac_priv *priv,
			      struct dwmac_rk_lb_priv *lb_priv)
{
	struct sk_buff *skb = lb_priv->tx_skbuff;
	struct dma_desc *p;

	p = lb_priv->dma_tx;

	if (likely(lb_priv->tx_skbuff_dma)) {
		dma_unmap_single(priv->device,
				 lb_priv->tx_skbuff_dma,
				 lb_priv->tx_skbuff_dma_len,
				 DMA_TO_DEVICE);
		lb_priv->tx_skbuff_dma = 0;
	}

	if (likely(skb)) {
		dev_consume_skb_any(skb);
		lb_priv->tx_skbuff = NULL;
	}

	priv->hw->desc->release_tx_desc(p, priv->mode);
}

static int dwmac_rk_xmit(struct sk_buff *skb, struct net_device *dev,
			 struct dwmac_rk_lb_priv *lb_priv)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	unsigned int nopaged_len = skb_headlen(skb);
	int csum_insertion = 0;
	struct dma_desc *desc;
	unsigned int des;

	csum_insertion = (skb->ip_summed == CHECKSUM_PARTIAL);

	desc = lb_priv->dma_tx;
	lb_priv->tx_skbuff = skb;

	des = dma_map_single(priv->device, skb->data,
			     nopaged_len, DMA_TO_DEVICE);
	if (dma_mapping_error(priv->device, des))
		goto dma_map_err;
	lb_priv->tx_skbuff_dma = des;

	stmmac_set_desc_addr(priv, desc, des);
	lb_priv->tx_skbuff_dma_len = nopaged_len;

	/* Prepare the first descriptor setting the OWN bit too */
	stmmac_prepare_tx_desc(priv, desc, 1, nopaged_len,
			       csum_insertion, priv->mode, 1, 1,
			       skb->len);
	stmmac_enable_dma_transmission(priv, priv->ioaddr, 0); //channel idx 0

	lb_priv->tx_tail_addr = lb_priv->dma_tx_phy + sizeof(*desc);
	stmmac_set_tx_tail_ptr(priv, priv->ioaddr, lb_priv->tx_tail_addr, 0);

	return 0;

dma_map_err:
	pr_err("%s: Tx dma map failed\n", __func__);
	dev_kfree_skb(skb);
	return -EFAULT;
}

static int __dwmac_rk_loopback_run(struct stmmac_priv *priv,
				   struct dwmac_rk_lb_priv *lb_priv)
{
	u32 rx_channels_count = min_t(u32, priv->plat->rx_queues_to_use, 1);
	u32 tx_channels_count = min_t(u32, priv->plat->tx_queues_to_use, 1);
	struct sk_buff *tx_skb;
	u32 chan = 0;
	int ret = -EIO, delay;
	u32 status;
	bool finish = false;

	if (lb_priv->speed == LOOPBACK_SPEED1000)
		delay = 10;
	else if (lb_priv->speed == LOOPBACK_SPEED100)
		delay = 20;
	else if (lb_priv->speed == LOOPBACK_SPEED10)
		delay = 50;
	else
		return -EPERM;

	if (dwmac_rk_rx_fill(priv, lb_priv))
		return -ENOMEM;

	/* Enable the MAC Rx/Tx */
	stmmac_mac_set(priv, priv->ioaddr, true);

	for (chan = 0; chan < rx_channels_count; chan++)
		stmmac_start_rx(priv, priv->ioaddr, chan);
	for (chan = 0; chan < tx_channels_count; chan++)
		stmmac_start_tx(priv, priv->ioaddr, chan);

	tx_skb = dwmac_rk_get_skb(priv, lb_priv);
	if (!tx_skb) {
		ret = -ENOMEM;
		goto stop;
	}

	if (dwmac_rk_xmit(tx_skb, priv->dev, lb_priv)) {
		ret = -EFAULT;
		goto stop;
	}

	do {
		usleep_range(100, 150);
		delay--;
		if (priv->plat->has_gmac4) {
			status = readl(priv->ioaddr + RK_DMA_CHAN_STATUS(0));
			finish = (status & RK_DMA_CHAN_STATUS_ERI) && (status & RK_DMA_CHAN_STATUS_ETI);
		} else {
			status = readl(priv->ioaddr + DMA_STATUS);
			finish = (status & DMA_STATUS_ERI) && (status & DMA_STATUS_ETI);
		}

		if (finish) {
			if (!dwmac_rk_get_desc_status(priv, lb_priv)) {
				ret = dwmac_rk_rx_validate(priv, lb_priv);
				break;
			}
		}
	} while (delay <= 0);
	writel((status & 0x1ffff), priv->ioaddr + DMA_STATUS);

stop:
	for (chan = 0; chan < rx_channels_count; chan++)
		stmmac_stop_rx(priv, priv->ioaddr, chan);
	for (chan = 0; chan < tx_channels_count; chan++)
		stmmac_stop_tx(priv, priv->ioaddr, chan);

	stmmac_mac_set(priv, priv->ioaddr, false);
	/* wait for state machine is disabled */
	usleep_range(100, 150);

	dwmac_rk_tx_clean(priv, lb_priv);
	dwmac_rk_rx_clean(priv, lb_priv);

	return ret;
}

static int dwmac_rk_loopback_with_identify(struct stmmac_priv *priv,
					   struct dwmac_rk_lb_priv *lb_priv,
					   int tx, int rx)
{
	lb_priv->id++;
	lb_priv->tx = tx;
	lb_priv->rx = rx;

	lb_priv->packet = &dwmac_rk_tcp_attr;
	dwmac_rk_set_rgmii_delayline(priv, tx, rx);

	return __dwmac_rk_loopback_run(priv, lb_priv);
}

static inline bool dwmac_rk_delayline_is_txvalid(struct dwmac_rk_lb_priv *lb_priv,
						 int tx)
{
	if (tx > 0 && tx < lb_priv->max_delay)
		return true;
	else
		return false;
}

static inline bool dwmac_rk_delayline_is_valid(struct dwmac_rk_lb_priv *lb_priv,
					       int tx, int rx)
{
	if ((tx > 0 && tx < lb_priv->max_delay) &&
	    (rx > 0 && rx < lb_priv->max_delay))
		return true;
	else
		return false;
}

static int dwmac_rk_delayline_scan_cross(struct stmmac_priv *priv,
					 struct dwmac_rk_lb_priv *lb_priv)
{
	int tx_left, tx_right, rx_up, rx_down;
	int i, j, tx_index, rx_index;
	int tx_mid = 0, rx_mid = 0;

	/* initiation */
	tx_index = SCAN_STEP;
	rx_index = SCAN_STEP;

re_scan:
	/* start from rx based on the experience */
	for (i = rx_index; i <= (lb_priv->max_delay - SCAN_STEP); i += SCAN_STEP) {
		tx_left = 0;
		tx_right = 0;
		tx_mid = 0;

		for (j = tx_index; j <= (lb_priv->max_delay - SCAN_STEP);
		     j += SCAN_STEP) {
			if (!dwmac_rk_loopback_with_identify(priv,
			    lb_priv, j, i)) {
				if (!tx_left)
					tx_left = j;
				tx_right = j;
			}
		}

		/* look for tx_mid */
		if ((tx_right - tx_left) > SCAN_VALID_RANGE) {
			tx_mid = (tx_right + tx_left) / 2;
			break;
		}
	}

	/* Worst case: reach the end */
	if (i >= (lb_priv->max_delay - SCAN_STEP))
		goto end;

	rx_up = 0;
	rx_down = 0;

	/* look for rx_mid base on the tx_mid */
	for (i = SCAN_STEP; i <= (lb_priv->max_delay - SCAN_STEP);
	     i += SCAN_STEP) {
		if (!dwmac_rk_loopback_with_identify(priv, lb_priv,
		    tx_mid, i)) {
			if (!rx_up)
				rx_up = i;
			rx_down = i;
		}
	}

	if ((rx_down - rx_up) > SCAN_VALID_RANGE) {
		/* Now get the rx_mid */
		rx_mid = (rx_up + rx_down) / 2;
	} else {
		rx_index += SCAN_STEP;
		rx_mid = 0;
		goto re_scan;
	}

	if (dwmac_rk_delayline_is_valid(lb_priv, tx_mid, rx_mid)) {
		lb_priv->final_tx = tx_mid;
		lb_priv->final_rx = rx_mid;

		pr_info("Find available tx_delay = 0x%02x, rx_delay = 0x%02x\n",
			lb_priv->final_tx, lb_priv->final_rx);

		return 0;
	}
end:
	pr_err("Can't find available delayline\n");
	return -ENXIO;
}

static int dwmac_rk_delayline_scan(struct stmmac_priv *priv,
				   struct dwmac_rk_lb_priv *lb_priv)
{
	int phy_iface = dwmac_rk_get_phy_interface(priv);
	int tx, rx, tx_sum, rx_sum, count;
	int tx_mid, rx_mid;
	int ret = -ENXIO;

	tx_sum = 0;
	rx_sum = 0;
	count = 0;

	for (rx = 0x0; rx <= lb_priv->max_delay; rx++) {
		if (phy_iface == PHY_INTERFACE_MODE_RGMII_RXID)
			rx = -1;
		printk(KERN_CONT "RX(%03d):", rx);
		for (tx = 0x0; tx <= lb_priv->max_delay; tx++) {
			if (!dwmac_rk_loopback_with_identify(priv,
			    lb_priv, tx, rx)) {
				tx_sum += tx;
				rx_sum += rx;
				count++;
				printk(KERN_CONT "O");
			} else {
				printk(KERN_CONT " ");
			}
		}
		printk(KERN_CONT "\n");

		if (phy_iface == PHY_INTERFACE_MODE_RGMII_RXID)
			break;
	}

	if (tx_sum && rx_sum && count) {
		tx_mid = tx_sum / count;
		rx_mid = rx_sum / count;

		if (phy_iface == PHY_INTERFACE_MODE_RGMII_RXID) {
			if (dwmac_rk_delayline_is_txvalid(lb_priv, tx_mid)) {
				lb_priv->final_tx = tx_mid;
				lb_priv->final_rx = -1;
				ret = 0;
			}
		} else {
			if (dwmac_rk_delayline_is_valid(lb_priv, tx_mid, rx_mid)) {
				lb_priv->final_tx = tx_mid;
				lb_priv->final_rx = rx_mid;
				ret = 0;
			}
		}
	}

	if (ret) {
		pr_err("\nCan't find suitable delayline\n");
	} else {
		if (phy_iface == PHY_INTERFACE_MODE_RGMII_RXID)
			pr_info("Find available tx_delay = 0x%02x, rx_delay = disable\n",
				lb_priv->final_tx);
		else
			pr_info("\nFind suitable tx_delay = 0x%02x, rx_delay = 0x%02x\n",
				lb_priv->final_tx, lb_priv->final_rx);
	}

	return ret;
}

static int dwmac_rk_loopback_delayline_scan(struct stmmac_priv *priv,
					    struct dwmac_rk_lb_priv *lb_priv)
{
	if (lb_priv->sysfs)
		return dwmac_rk_delayline_scan(priv, lb_priv);
	else
		return dwmac_rk_delayline_scan_cross(priv, lb_priv);
}

static void dwmac_rk_dma_free_rx_skbufs(struct stmmac_priv *priv,
					struct dwmac_rk_lb_priv *lb_priv)
{
	if (lb_priv->rx_skbuff) {
		dma_unmap_single(priv->device, lb_priv->rx_skbuff_dma,
				 lb_priv->dma_buf_sz, DMA_FROM_DEVICE);
		dev_kfree_skb_any(lb_priv->rx_skbuff);
	}
	lb_priv->rx_skbuff = NULL;
}

static void dwmac_rk_dma_free_tx_skbufs(struct stmmac_priv *priv,
					struct dwmac_rk_lb_priv *lb_priv)
{
	if (lb_priv->tx_skbuff_dma) {
		dma_unmap_single(priv->device,
				 lb_priv->tx_skbuff_dma,
				 lb_priv->tx_skbuff_dma_len,
				 DMA_TO_DEVICE);
	}

	if (lb_priv->tx_skbuff) {
		dev_kfree_skb_any(lb_priv->tx_skbuff);
		lb_priv->tx_skbuff = NULL;
		lb_priv->tx_skbuff_dma = 0;
	}
}

static int dwmac_rk_init_dma_desc_rings(struct net_device *dev, gfp_t flags,
					struct dwmac_rk_lb_priv *lb_priv)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	struct dma_desc *p;

	p = lb_priv->dma_tx;
	p->des2 = 0;
	lb_priv->tx_skbuff_dma = 0;
	lb_priv->tx_skbuff_dma_len = 0;
	lb_priv->tx_skbuff = NULL;

	lb_priv->rx_skbuff = NULL;
	stmmac_init_rx_desc(priv, lb_priv->dma_rx,
				     priv->use_riwt, priv->mode,
				     true, lb_priv->dma_buf_sz);

	stmmac_init_tx_desc(priv, lb_priv->dma_tx,
				     priv->mode,
				     true);

	return 0;
}

static int dwmac_rk_alloc_dma_desc_resources(struct stmmac_priv *priv,
					     struct dwmac_rk_lb_priv *lb_priv)
{
	int ret = -ENOMEM;

	/* desc dma map */
	lb_priv->dma_rx = dma_alloc_coherent(priv->device,
					     sizeof(struct dma_desc),
					     &lb_priv->dma_rx_phy,
					     GFP_KERNEL);
	if (!lb_priv->dma_rx)
		return ret;

	lb_priv->dma_tx = dma_alloc_coherent(priv->device,
					     sizeof(struct dma_desc),
					     &lb_priv->dma_tx_phy,
					     GFP_KERNEL);
	if (!lb_priv->dma_tx) {
		dma_free_coherent(priv->device,
				  sizeof(struct dma_desc),
				  lb_priv->dma_rx, lb_priv->dma_rx_phy);
		return ret;
	}

	return 0;
}

static void dwmac_rk_free_dma_desc_resources(struct stmmac_priv *priv,
					     struct dwmac_rk_lb_priv *lb_priv)
{
	/* Release the DMA TX/RX socket buffers */
	dwmac_rk_dma_free_rx_skbufs(priv, lb_priv);
	dwmac_rk_dma_free_tx_skbufs(priv, lb_priv);

	dma_free_coherent(priv->device, sizeof(struct dma_desc),
			  lb_priv->dma_tx, lb_priv->dma_tx_phy);
	dma_free_coherent(priv->device, sizeof(struct dma_desc),
			  lb_priv->dma_rx, lb_priv->dma_rx_phy);
}

static int dwmac_rk_init_dma_engine(struct stmmac_priv *priv,
				    struct dwmac_rk_lb_priv *lb_priv)
{
	u32 rx_channels_count = min_t(u32, priv->plat->rx_queues_to_use, 1);
	u32 tx_channels_count = min_t(u32, priv->plat->tx_queues_to_use, 1);
	u32 dma_csr_ch = max(rx_channels_count, tx_channels_count);
	u32 chan = 0;
	int ret = 0;

	ret = stmmac_reset(priv, priv->ioaddr);
	if (ret) {
		dev_err(priv->device, "Failed to reset the dma\n");
		return ret;
	}

	/* DMA Configuration */
	stmmac_dma_init(priv, priv->ioaddr, priv->plat->dma_cfg);

	if (priv->plat->axi)
		stmmac_axi(priv, priv->ioaddr, priv->plat->axi);

	for (chan = 0; chan < dma_csr_ch; chan++)
		stmmac_init_chan(priv, priv->ioaddr, priv->plat->dma_cfg, 0);

	/* DMA RX Channel Configuration */
	for (chan = 0; chan < rx_channels_count; chan++) {
		stmmac_init_rx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    lb_priv->dma_rx_phy, 0);

		lb_priv->rx_tail_addr = lb_priv->dma_rx_phy +
			    (1 * sizeof(struct dma_desc));
		stmmac_set_rx_tail_ptr(priv, priv->ioaddr,
				       lb_priv->rx_tail_addr, 0);
	}

	/* DMA TX Channel Configuration */
	for (chan = 0; chan < tx_channels_count; chan++) {
		stmmac_init_tx_chan(priv, priv->ioaddr, priv->plat->dma_cfg,
				    lb_priv->dma_tx_phy, chan);

		lb_priv->tx_tail_addr = lb_priv->dma_tx_phy;
		stmmac_set_tx_tail_ptr(priv, priv->ioaddr,
				       lb_priv->tx_tail_addr, chan);
	}

	return ret;
}

static void dwmac_rk_dma_operation_mode(struct stmmac_priv *priv,
					struct dwmac_rk_lb_priv *lb_priv)
{
	u32 rx_channels_count = min_t(u32, priv->plat->rx_queues_to_use, 1);
	u32 tx_channels_count = min_t(u32, priv->plat->tx_queues_to_use, 1);
	int rxfifosz = priv->plat->rx_fifo_size;
	int txfifosz = priv->plat->tx_fifo_size;
	u32 txmode = SF_DMA_MODE;
	u32 rxmode = SF_DMA_MODE;
	u32 chan = 0;
	u8 qmode = 0;

	if (rxfifosz == 0)
		rxfifosz = priv->dma_cap.rx_fifo_size;
	if (txfifosz == 0)
		txfifosz = priv->dma_cap.tx_fifo_size;

	/* Adjust for real per queue fifo size */
	rxfifosz /= rx_channels_count;
	txfifosz /= tx_channels_count;

	/* configure all channels */
	for (chan = 0; chan < rx_channels_count; chan++) {
		qmode = priv->plat->rx_queues_cfg[chan].mode_to_use;

		stmmac_dma_rx_mode(priv, priv->ioaddr, rxmode, chan,
				   rxfifosz, qmode);
		stmmac_set_dma_bfsize(priv, priv->ioaddr, lb_priv->dma_buf_sz,
				      chan);
	}

	for (chan = 0; chan < tx_channels_count; chan++) {
		qmode = priv->plat->tx_queues_cfg[chan].mode_to_use;

		stmmac_dma_tx_mode(priv, priv->ioaddr, txmode, chan,
				   txfifosz, qmode);
	}
}

static void dwmac_rk_rx_queue_dma_chan_map(struct stmmac_priv *priv)
{
	u32 rx_queues_count = min_t(u32, priv->plat->rx_queues_to_use, 1);
	u32 queue;
	u32 chan;

	for (queue = 0; queue < rx_queues_count; queue++) {
		chan = priv->plat->rx_queues_cfg[queue].chan;
		stmmac_map_mtl_to_dma(priv, priv->hw, queue, chan);
	}
}

static void dwmac_rk_mac_enable_rx_queues(struct stmmac_priv *priv)
{
	u32 rx_queues_count = min_t(u32, priv->plat->rx_queues_to_use, 1);
	int queue;
	u8 mode;

	for (queue = 0; queue < rx_queues_count; queue++) {
		mode = priv->plat->rx_queues_cfg[queue].mode_to_use;
		stmmac_rx_queue_enable(priv, priv->hw, mode, queue);
	}
}

static void dwmac_rk_mtl_configuration(struct stmmac_priv *priv)
{
	/* Map RX MTL to DMA channels */
	dwmac_rk_rx_queue_dma_chan_map(priv);

	/* Enable MAC RX Queues */
	dwmac_rk_mac_enable_rx_queues(priv);
}

static void dwmac_rk_mmc_setup(struct stmmac_priv *priv)
{
	unsigned int mode = MMC_CNTRL_RESET_ON_READ | MMC_CNTRL_COUNTER_RESET |
			    MMC_CNTRL_PRESET | MMC_CNTRL_FULL_HALF_PRESET;

	stmmac_mmc_intr_all_mask(priv, priv->mmcaddr);

	if (priv->dma_cap.rmon) {
		stmmac_mmc_ctrl(priv, priv->mmcaddr, mode);
		memset(&priv->mmc, 0, sizeof(struct stmmac_counters));
	} else {
		netdev_info(priv->dev, "No MAC Management Counters available\n");
	}
}

static int dwmac_rk_init(struct net_device *dev,
			 struct dwmac_rk_lb_priv *lb_priv)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	int ret;
	u32 mode;

	lb_priv->dma_buf_sz = 1536; /* mtu 1500 size */

	if (priv->plat->has_gmac4)
		lb_priv->buf_sz = priv->dma_cap.rx_fifo_size; /* rx fifo size */
	else
		lb_priv->buf_sz = 4096; /* rx fifo size */

	ret = dwmac_rk_alloc_dma_desc_resources(priv, lb_priv);
	if (ret < 0) {
		pr_err("%s: DMA descriptors allocation failed\n", __func__);
		return ret;
	}

	ret = dwmac_rk_init_dma_desc_rings(dev, GFP_KERNEL, lb_priv);
	if (ret < 0) {
		pr_err("%s: DMA descriptors initialization failed\n", __func__);
		goto init_error;
	}

	/* DMA initialization and SW reset */
	ret = dwmac_rk_init_dma_engine(priv, lb_priv);
	if (ret < 0) {
		pr_err("%s: DMA engine initialization failed\n", __func__);
		goto init_error;
	}

	/* Copy the MAC addr into the HW  */
	priv->hw->mac->set_umac_addr(priv->hw, dev->dev_addr, 0);

	/* Initialize the MAC Core */
	stmmac_core_init(priv, priv->hw, dev);

	dwmac_rk_mtl_configuration(priv);

	dwmac_rk_mmc_setup(priv);

	ret = priv->hw->mac->rx_ipc(priv->hw);
	if (!ret) {
		pr_warn(" RX IPC Checksum Offload disabled\n");
		priv->plat->rx_coe = STMMAC_RX_COE_NONE;
		priv->hw->rx_csum = 0;
	}

	/* Set the HW DMA mode and the COE */
	dwmac_rk_dma_operation_mode(priv, lb_priv);

	if (priv->plat->has_gmac4) {
		mode = readl(priv->ioaddr + RK_DMA_CHAN_TX_CONTROL(0));
		/* Disable OSP to get best performance */
		mode &= ~RK_DMA_CONTROL_OSP;
		writel(mode, priv->ioaddr + RK_DMA_CHAN_TX_CONTROL(0));
	} else {
		/* Disable OSF */
		mode = readl(priv->ioaddr + DMA_CONTROL);
		writel((mode & ~DMA_CONTROL_OSF), priv->ioaddr + DMA_CONTROL);
	}

	stmmac_enable_dma_irq(priv, priv->ioaddr, 0, 1, 1);

	if (priv->hw->pcs)
		stmmac_pcs_ctrl_ane(priv, 1, priv->hw->ps, 0);

	return 0;
init_error:
	dwmac_rk_free_dma_desc_resources(priv, lb_priv);

	return ret;
}

static void dwmac_rk_release(struct net_device *dev,
			     struct dwmac_rk_lb_priv *lb_priv)
{
	struct stmmac_priv *priv = netdev_priv(dev);

	stmmac_disable_dma_irq(priv, priv->ioaddr, 0, 0, 0);

	/* Release and free the Rx/Tx resources */
	dwmac_rk_free_dma_desc_resources(priv, lb_priv);
}

static int dwmac_rk_get_max_delayline(struct stmmac_priv *priv)
{
	if (of_device_is_compatible(priv->device->of_node,
				    "rockchip,rk3588-gmac"))
		return RK3588_MAX_DELAYLINE;
	else
		return MAX_DELAYLINE;
}

static int dwmac_rk_phy_poll_reset(struct stmmac_priv *priv, int addr)
{
	/* Poll until the reset bit clears (50ms per retry == 0.6 sec) */
	unsigned int val, retries = 12;
	int ret;

	val = mdiobus_read(priv->mii, addr, MII_BMCR);
	mdiobus_write(priv->mii, addr, MII_BMCR, val | BMCR_RESET);

	do {
		msleep(50);
		ret = mdiobus_read(priv->mii, addr, MII_BMCR);
		if (ret < 0)
			return ret;
	} while (ret & BMCR_RESET && --retries);
	if (ret & BMCR_RESET)
		return -ETIMEDOUT;

	msleep(1);
	return 0;
}

static int dwmac_rk_loopback_run(struct stmmac_priv *priv,
				 struct dwmac_rk_lb_priv *lb_priv)
{
	struct net_device *ndev = priv->dev;
	int phy_iface = dwmac_rk_get_phy_interface(priv);
	int ndev_up, phy_addr;
	int ret = -EINVAL;

	if (!ndev || !priv->mii)
		return -EINVAL;

	if (!ndev->phydev) {
		pr_warn("Try again later, after phy is bound\n");
		return -EAGAIN;
	}

	phy_addr = ndev->phydev->mdio.addr;
	lb_priv->max_delay = dwmac_rk_get_max_delayline(priv);

	rtnl_lock();
	/* check the netdevice up or not */
	ndev_up = ndev->flags & IFF_UP;

	if (ndev_up) {
		if (!netif_running(ndev) || !ndev->phydev) {
			rtnl_unlock();
			return -EINVAL;
		}

		/* check if the negotiation status */
		if (ndev->phydev->state != PHY_NOLINK &&
		    ndev->phydev->state != PHY_RUNNING) {
			rtnl_unlock();
			pr_warn("Try again later, after negotiation done\n");
			return -EAGAIN;
		}

		ndev->netdev_ops->ndo_stop(ndev);

		if (priv->plat->stmmac_rst)
			reset_control_assert(priv->plat->stmmac_rst);
		dwmac_rk_phy_poll_reset(priv, phy_addr);
		if (priv->plat->stmmac_rst)
			reset_control_deassert(priv->plat->stmmac_rst);
	}
	/* wait for phy and controller ready */
	usleep_range(100000, 200000);

	dwmac_rk_set_loopback(priv, lb_priv->type, lb_priv->speed,
			      true, phy_addr, true);

	ret = dwmac_rk_init(ndev, lb_priv);
	if (ret)
		goto exit_init;

	dwmac_rk_set_loopback(priv, lb_priv->type, lb_priv->speed,
			      true, phy_addr, false);

	if (lb_priv->scan) {
		/* scan only support for rgmii mode */
		if (phy_iface != PHY_INTERFACE_MODE_RGMII &&
		    phy_iface != PHY_INTERFACE_MODE_RGMII_ID &&
		    phy_iface != PHY_INTERFACE_MODE_RGMII_RXID &&
		    phy_iface != PHY_INTERFACE_MODE_RGMII_TXID) {
			ret = -EINVAL;
			goto out;
		}
		ret = dwmac_rk_loopback_delayline_scan(priv, lb_priv);
	} else {
		lb_priv->id++;
		lb_priv->tx = 0;
		lb_priv->rx = 0;

		lb_priv->packet = &dwmac_rk_tcp_attr;
		ret = __dwmac_rk_loopback_run(priv, lb_priv);
	}

out:
	dwmac_rk_release(ndev, lb_priv);
	dwmac_rk_set_loopback(priv, lb_priv->type, lb_priv->speed,
			      false, phy_addr, false);
exit_init:
	if (ndev_up)
		ndev->netdev_ops->ndo_open(ndev);

	rtnl_unlock();

	return ret;
}

static ssize_t rgmii_delayline_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int tx, rx;

	dwmac_rk_get_rgmii_delayline(priv, &tx, &rx);

	return sprintf(buf, "tx delayline: 0x%x, rx delayline: 0x%x\n",
		       tx, rx);
}

static ssize_t rgmii_delayline_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int tx = 0, rx = 0;
	char tmp[32];
	size_t buf_size = min(count, (sizeof(tmp) - 1));
	char *data;

	memset(tmp, 0, sizeof(tmp));
	strncpy(tmp, buf, buf_size);

	data = tmp;
	data = strstr(data, " ");
	if (!data)
		goto out;
	*data = 0;
	data++;

	if (kstrtoint(tmp, 0, &tx) || tx > dwmac_rk_get_max_delayline(priv))
		goto out;

	if (kstrtoint(data, 0, &rx) || rx > dwmac_rk_get_max_delayline(priv))
		goto out;

	dwmac_rk_set_rgmii_delayline(priv, tx, rx);
	pr_info("Set rgmii delayline tx: 0x%x, rx: 0x%x\n", tx, rx);

	return count;
out:
	pr_err("wrong delayline value input, range is <0x0, 0x7f>\n");
	pr_err("usage: <tx_delayline> <rx_delayline>\n");

	return count;
}
static DEVICE_ATTR_RW(rgmii_delayline);

static ssize_t mac_lb_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct dwmac_rk_lb_priv *lb_priv;
	int ret, speed;

	lb_priv = kzalloc(sizeof(*lb_priv), GFP_KERNEL);
	if (!lb_priv)
		return -ENOMEM;

	ret = kstrtoint(buf, 0, &speed);
	if (ret) {
		kfree(lb_priv);
		return count;
	}
	pr_info("MAC loopback speed set to %d\n", speed);

	lb_priv->sysfs = 1;
	lb_priv->type = LOOPBACK_TYPE_GMAC;
	lb_priv->speed = speed;
	lb_priv->scan = 0;

	ret = dwmac_rk_loopback_run(priv, lb_priv);
	kfree(lb_priv);

	if (!ret)
		pr_info("MAC loopback: PASS\n");
	else
		pr_info("MAC loopback: FAIL\n");

	return count;
}
static DEVICE_ATTR_WO(mac_lb);

static ssize_t phy_lb_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct dwmac_rk_lb_priv *lb_priv;
	int ret, speed;

	lb_priv = kzalloc(sizeof(*lb_priv), GFP_KERNEL);
	if (!lb_priv)
		return  -ENOMEM;

	ret = kstrtoint(buf, 0, &speed);
	if (ret) {
		kfree(lb_priv);
		return count;
	}
	pr_info("PHY loopback speed set to %d\n", speed);

	lb_priv->sysfs = 1;
	lb_priv->type = LOOPBACK_TYPE_PHY;
	lb_priv->speed = speed;
	lb_priv->scan = 0;

	ret = dwmac_rk_loopback_run(priv, lb_priv);
	if (!ret)
		pr_info("PHY loopback: PASS\n");
	else
		pr_info("PHY loopback: FAIL\n");

	kfree(lb_priv);
	return count;
}
static DEVICE_ATTR_WO(phy_lb);

static ssize_t phy_lb_scan_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct dwmac_rk_lb_priv *lb_priv;
	int ret, speed;

	lb_priv = kzalloc(sizeof(*lb_priv), GFP_KERNEL);
	if (!lb_priv)
		return -ENOMEM;

	ret = kstrtoint(buf, 0, &speed);
	if (ret) {
		kfree(lb_priv);
		return count;
	}
	pr_info("Delayline scan speed set to %d\n", speed);

	lb_priv->sysfs = 1;
	lb_priv->type = LOOPBACK_TYPE_PHY;
	lb_priv->speed = speed;
	lb_priv->scan = 1;

	dwmac_rk_loopback_run(priv, lb_priv);

	kfree(lb_priv);
	return count;
}
static DEVICE_ATTR_WO(phy_lb_scan);

static int dwmac_rk_create_loopback_sysfs(struct device *device)
{
	int ret;

	ret = device_create_file(device, &dev_attr_rgmii_delayline);
	if (ret)
		return ret;

	ret = device_create_file(device, &dev_attr_mac_lb);
	if (ret)
		goto remove_rgmii_delayline;

	ret = device_create_file(device, &dev_attr_phy_lb);
	if (ret)
		goto remove_mac_lb;

	ret = device_create_file(device, &dev_attr_phy_lb_scan);
	if (ret)
		goto remove_phy_lb;

	return 0;

remove_rgmii_delayline:
	device_remove_file(device, &dev_attr_rgmii_delayline);

remove_mac_lb:
	device_remove_file(device, &dev_attr_mac_lb);

remove_phy_lb:
	device_remove_file(device, &dev_attr_phy_lb);

	return ret;
}

static int dwmac_rk_remove_loopback_sysfs(struct device *device)
{
	device_remove_file(device, &dev_attr_rgmii_delayline);
	device_remove_file(device, &dev_attr_mac_lb);
	device_remove_file(device, &dev_attr_phy_lb);
	device_remove_file(device, &dev_attr_phy_lb_scan);

	return 0;
}

/*
 * End delay scanning
 */

static int rk_gmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	const struct rk_gmac_ops *data;
	int ret;

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "no of match data provided\n");
		return -EINVAL;
	}

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	/* If the stmmac is not already selected as gmac4,
	 * then make sure we fallback to gmac.
	 */
	if (!plat_dat->has_gmac4) {
		plat_dat->has_gmac = true;
		plat_dat->rx_fifo_size = 4096;
		plat_dat->tx_fifo_size = 2048;
	}

	plat_dat->get_interfaces = rk_get_interfaces;
	plat_dat->set_clk_tx_rate = rk_set_clk_tx_rate;
	plat_dat->suspend = rk_gmac_suspend;
	plat_dat->resume = rk_gmac_resume;

	plat_dat->bsp_priv = rk_gmac_setup(pdev, plat_dat, data);
	if (IS_ERR(plat_dat->bsp_priv))
		return PTR_ERR(plat_dat->bsp_priv);

	ret = rk_gmac_clk_init(plat_dat);
	if (ret)
		return ret;

	ret = rk_gmac_powerup(plat_dat->bsp_priv);
	if (ret)
		return ret;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_gmac_powerdown;

	ret = dwmac_rk_create_loopback_sysfs(&pdev->dev);
	if (ret)
		goto err_gmac_powerdown;

	return 0;

err_gmac_powerdown:
	rk_gmac_powerdown(plat_dat->bsp_priv);

	return ret;
}

static void rk_gmac_remove(struct platform_device *pdev)
{
	struct stmmac_priv *priv = netdev_priv(platform_get_drvdata(pdev));
	struct rk_priv_data *bsp_priv = priv->plat->bsp_priv;

	dwmac_rk_remove_loopback_sysfs(&pdev->dev);

	stmmac_dvr_remove(&pdev->dev);

	rk_gmac_powerdown(bsp_priv);

	if (priv->plat->phy_node && bsp_priv->integrated_phy)
		clk_put(bsp_priv->clk_phy);
}

static const struct of_device_id rk_gmac_dwmac_match[] = {
	{ .compatible = "rockchip,px30-gmac",	.data = &px30_ops   },
	{ .compatible = "rockchip,rk3128-gmac", .data = &rk3128_ops },
	{ .compatible = "rockchip,rk3228-gmac", .data = &rk3228_ops },
	{ .compatible = "rockchip,rk3288-gmac", .data = &rk3288_ops },
	{ .compatible = "rockchip,rk3308-gmac", .data = &rk3308_ops },
	{ .compatible = "rockchip,rk3328-gmac", .data = &rk3328_ops },
	{ .compatible = "rockchip,rk3366-gmac", .data = &rk3366_ops },
	{ .compatible = "rockchip,rk3368-gmac", .data = &rk3368_ops },
	{ .compatible = "rockchip,rk3399-gmac", .data = &rk3399_ops },
	{ .compatible = "rockchip,rk3528-gmac", .data = &rk3528_ops },
	{ .compatible = "rockchip,rk3568-gmac", .data = &rk3568_ops },
	{ .compatible = "rockchip,rk3576-gmac", .data = &rk3576_ops },
	{ .compatible = "rockchip,rk3588-gmac", .data = &rk3588_ops },
	{ .compatible = "rockchip,rv1108-gmac", .data = &rv1108_ops },
	{ .compatible = "rockchip,rv1126-gmac", .data = &rv1126_ops },
	{ }
};
MODULE_DEVICE_TABLE(of, rk_gmac_dwmac_match);

static struct platform_driver rk_gmac_dwmac_driver = {
	.probe  = rk_gmac_probe,
	.remove = rk_gmac_remove,
	.driver = {
		.name           = "rk_gmac-dwmac",
		.pm		= &stmmac_simple_pm_ops,
		.of_match_table = rk_gmac_dwmac_match,
	},
};
module_platform_driver(rk_gmac_dwmac_driver);

MODULE_AUTHOR("Chen-Zhi (Roger Chen) <roger.chen@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK3288 DWMAC specific glue layer");
MODULE_LICENSE("GPL");
