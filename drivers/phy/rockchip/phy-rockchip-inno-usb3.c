/*
 * Rockchip USB 3.0 PHY with Innosilicon IP block driver
 *
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/usb/phy.h>

#define BIT_WRITEABLE_SHIFT	16
#define SCHEDULE_DELAY	(60 * HZ)

#define U3PHY_APB_RST	BIT(0)
#define U3PHY_POR_RST	BIT(1)
#define U3PHY_MAC_RST	BIT(2)

enum rockchip_usb3phy_port_id {
	USB3PHY_PORT_PIPE,
	USB3PHY_PORT_UTMI,
	USB3PHY_NUM_PORTS,
};

enum rockchip_usb3phy_pipe_pwr {
	PIPE_PWR_P0	= 0,
	PIPE_PWR_P1	= 1,
	PIPE_PWR_P2	= 2,
	PIPE_PWR_P3	= 3,
	PIPE_PWR_MAX	= 4,
};

enum rockchip_usb3phy_utmi_state {
	PHY_UTMI_HS_ONLINE	= 0,
	PHY_UTMI_DISCONNECT	= 1,
	PHY_UTMI_CONNECT	= 2,
	PHY_UTMI_FS_LS_ONLINE	= 4,
};

/*
 * @rvalue: reset value
 * @dvalue: desired value
 */
struct usb3phy_reg {
	unsigned int	offset;
	unsigned int	bitend;
	unsigned int	bitstart;
	unsigned int	rvalue;
	unsigned int	dvalue;
};

struct rockchip_usb3phy_grfcfg {
	struct usb3phy_reg	um_suspend;
	struct usb3phy_reg	ls_det_en;
	struct usb3phy_reg	ls_det_st;
	struct usb3phy_reg	um_ls;
	struct usb3phy_reg	um_hstdct;
	struct usb3phy_reg	pp_pwr_st;
	struct usb3phy_reg	pp_pwr_en[PIPE_PWR_MAX];
};

/**
 * struct rockchip_usb3phy_apbcfg: usb3-phy apb configuration.
 * @u2_pre_emp: usb2-phy pre-emphasis tuning.
 * @u2_pre_emp_sth: usb2-phy pre-emphasis strength tuning.
 * @u2_odt_tuning: usb2-phy odt 45ohm tuning.
 */
struct rockchip_usb3phy_apbcfg {
	unsigned int	u2_pre_emp;
	unsigned int	u2_pre_emp_sth;
	unsigned int	u2_odt_tuning;
};

struct rockchip_usb3phy;
struct rockchip_usb3phy_port;

struct rockchip_usb3phy_cfg {
	unsigned int reg;
	const struct rockchip_usb3phy_grfcfg grfcfg;
	const struct of_device_id *ports;

	int (*phy_pipe_power)(struct rockchip_usb3phy *,
			      struct rockchip_usb3phy_port *,
			      bool on);
	int (*phy_tuning)(struct rockchip_usb3phy *,
			  struct rockchip_usb3phy_port *,
			  struct device_node *);
};

struct rockchip_usb3phy_port {
	struct device *port_dev;
	struct phy *phy;
	void __iomem *base;
	enum rockchip_usb3phy_port_id type;
	struct reset_control *rst_por;
	struct reset_control *rst_apb;
	struct reset_control *rst_mac;
	struct clk *clk;
	struct phy_provider *provider;

	bool		suspended;
	bool		refclk_25m_quirk;
	struct mutex	mutex; /* mutex for updating register */
	struct delayed_work	um_sm_work;
};

struct rockchip_usb3phy {
	struct device *dev;
	struct regmap *usb3phy_grf;
	int um_ls_irq;
	struct rockchip_usb3phy_apbcfg apbcfg;
	const struct rockchip_usb3phy_cfg *cfgs;
	struct rockchip_usb3phy_port ports[USB3PHY_NUM_PORTS];
	struct usb_phy usb_phy;
};

static inline int param_write(void __iomem *base,
			      const struct usb3phy_reg *reg, bool desired)
{
	unsigned int val, mask;
	unsigned int tmp = desired ? reg->dvalue : reg->rvalue;
	int ret = 0;

	mask = GENMASK(reg->bitend, reg->bitstart);
	val = (tmp << reg->bitstart) | (mask << BIT_WRITEABLE_SHIFT);
	ret = regmap_write(base, reg->offset, val);

	return ret;
}

static inline bool param_expect(void __iomem *base,
			       const struct usb3phy_reg *reg,
			       unsigned int value)
{
	int ret;
	unsigned int tmp, orig;
	unsigned int mask = GENMASK(reg->bitend, reg->bitstart);

	ret = regmap_read(base, reg->offset, &orig);
	if (ret)
		return false;

	tmp = (orig & mask) >> reg->bitstart;
	return tmp == value;
}

static int rockchip_usb3phy_init(struct phy *phy)
{
	return 0;
}

static int rockchip_usb3phy_exit(struct phy *phy)
{
	return 0;
}

static int rockchip_usb3phy_power_on(struct phy *phy)
{
	struct rockchip_usb3phy_port *usb3phy_port = phy_get_drvdata(phy);
	struct rockchip_usb3phy *usb3phy = dev_get_drvdata(phy->dev.parent);
	int ret;

	dev_info(&usb3phy_port->phy->dev, "usb3phy %s power on\n",
		 (usb3phy_port->type == USB3PHY_PORT_UTMI) ? "u2" : "u3");

	if (!usb3phy_port->suspended)
		return 0;

	ret = clk_prepare_enable(usb3phy_port->clk);
	if (ret)
		return ret;

	if (usb3phy_port->type == USB3PHY_PORT_UTMI) {
		param_write(usb3phy->usb3phy_grf,
			    &usb3phy->cfgs->grfcfg.um_suspend, false);
	} else {
		/* current in p2 ? */
		if (param_expect(usb3phy->usb3phy_grf,
				&usb3phy->cfgs->grfcfg.pp_pwr_st, PIPE_PWR_P2))
			goto done;

		if (usb3phy->cfgs->phy_pipe_power) {
			dev_dbg(usb3phy->dev, "do pipe power up\n");
			usb3phy->cfgs->phy_pipe_power(usb3phy, usb3phy_port, true);
		}

		/* exit to p0 */
		param_write(usb3phy->usb3phy_grf,
			    &usb3phy->cfgs->grfcfg.pp_pwr_en[PIPE_PWR_P0], true);
		usleep_range(90, 100);

		/* enter to p2 from p0 */
		param_write(usb3phy->usb3phy_grf,
			    &usb3phy->cfgs->grfcfg.pp_pwr_en[PIPE_PWR_P2],
			    false);
		udelay(3);
	}

done:
	usb3phy_port->suspended = false;
	return 0;
}

static int rockchip_usb3phy_power_off(struct phy *phy)
{
	struct rockchip_usb3phy_port *usb3phy_port = phy_get_drvdata(phy);
	struct rockchip_usb3phy *usb3phy = dev_get_drvdata(phy->dev.parent);

	dev_info(&usb3phy_port->phy->dev, "usb3phy %s power off\n",
		 (usb3phy_port->type == USB3PHY_PORT_UTMI) ? "u2" : "u3");

	if (usb3phy_port->suspended)
		return 0;

	if (usb3phy_port->type == USB3PHY_PORT_UTMI) {
		param_write(usb3phy->usb3phy_grf,
			    &usb3phy->cfgs->grfcfg.um_suspend, true);
	} else {
		/* current in p3 ? */
		if (param_expect(usb3phy->usb3phy_grf,
				&usb3phy->cfgs->grfcfg.pp_pwr_st, PIPE_PWR_P3))
			goto done;

		/* exit to p0 */
		param_write(usb3phy->usb3phy_grf,
			    &usb3phy->cfgs->grfcfg.pp_pwr_en[PIPE_PWR_P0], true);
		udelay(2);

		/* enter to p3 from p0 */
		param_write(usb3phy->usb3phy_grf,
			    &usb3phy->cfgs->grfcfg.pp_pwr_en[PIPE_PWR_P3], true);
		udelay(6);

		if (usb3phy->cfgs->phy_pipe_power) {
			dev_dbg(usb3phy->dev, "do pipe power down\n");
			usb3phy->cfgs->phy_pipe_power(usb3phy, usb3phy_port, false);
		}
	}

done:
	clk_disable_unprepare(usb3phy_port->clk);
	usb3phy_port->suspended = true;
	return 0;
}

static struct phy_ops rockchip_usb3phy_ops = {
	.init		= rockchip_usb3phy_init,
	.exit		= rockchip_usb3phy_exit,
	.power_on	= rockchip_usb3phy_power_on,
	.power_off	= rockchip_usb3phy_power_off,
	.owner		= THIS_MODULE,
};

/*
 * The function manage host-phy port state and suspend/resume phy port
 * to save power automatically.
 *
 * we rely on utmi_linestate and utmi_hostdisconnect to identify whether
 * devices is disconnect or not. Besides, we do not need care it is FS/LS
 * disconnected or HS disconnected, actually, we just only need get the
 * device is disconnected at last through rearm the delayed work,
 * to suspend the phy port in _PHY_STATE_DISCONNECT_ case.
 */
static void rockchip_usb3phy_um_sm_work(struct work_struct *work)
{
	struct rockchip_usb3phy_port *usb3phy_port =
		container_of(work, struct rockchip_usb3phy_port, um_sm_work.work);
	struct rockchip_usb3phy *usb3phy =
		dev_get_drvdata(usb3phy_port->phy->dev.parent);
	unsigned int sh = usb3phy->cfgs->grfcfg.um_hstdct.bitend -
			usb3phy->cfgs->grfcfg.um_hstdct.bitstart + 1;
	unsigned int ul, uhd, state;
	unsigned int ul_mask, uhd_mask;
	int ret;

	mutex_lock(&usb3phy_port->mutex);

	ret = regmap_read(usb3phy->usb3phy_grf,
			  usb3phy->cfgs->grfcfg.um_ls.offset, &ul);
	if (ret < 0)
		goto next_schedule;

	ret = regmap_read(usb3phy->usb3phy_grf,
			  usb3phy->cfgs->grfcfg.um_hstdct.offset, &uhd);
	if (ret < 0)
		goto next_schedule;

	uhd_mask = GENMASK(usb3phy->cfgs->grfcfg.um_hstdct.bitend,
			   usb3phy->cfgs->grfcfg.um_hstdct.bitstart);
	ul_mask = GENMASK(usb3phy->cfgs->grfcfg.um_ls.bitend,
			  usb3phy->cfgs->grfcfg.um_ls.bitstart);

	/* switch on um_ls and um_hstdct as phy state */
	state = ((uhd & uhd_mask) >> usb3phy->cfgs->grfcfg.um_hstdct.bitstart) |
		(((ul & ul_mask) >> usb3phy->cfgs->grfcfg.um_ls.bitstart) << sh);

	switch (state) {
	case PHY_UTMI_HS_ONLINE:
		dev_dbg(&usb3phy_port->phy->dev, "HS online\n");
		break;
	case PHY_UTMI_FS_LS_ONLINE:
		/*
		 * For FS/LS device, the online state share with connect state
		 * from um_ls and um_hstdct register, so we distinguish
		 * them via suspended flag.
		 *
		 * Plus, there are two cases, one is D- Line pull-up, and D+
		 * line pull-down, the state is 4; another is D+ line pull-up,
		 * and D- line pull-down, the state is 2.
		 */
		if (!usb3phy_port->suspended) {
			/* D- line pull-up, D+ line pull-down */
			dev_dbg(&usb3phy_port->phy->dev, "FS/LS online\n");
			break;
		}
		/* fall through */
	case PHY_UTMI_CONNECT:
		if (usb3phy_port->suspended) {
			dev_dbg(&usb3phy_port->phy->dev, "Connected\n");
			rockchip_usb3phy_power_on(usb3phy_port->phy);
			usb3phy_port->suspended = false;
		} else {
			/* D+ line pull-up, D- line pull-down */
			dev_dbg(&usb3phy_port->phy->dev, "FS/LS online\n");
		}
		break;
	case PHY_UTMI_DISCONNECT:
		if (!usb3phy_port->suspended) {
			dev_dbg(&usb3phy_port->phy->dev, "Disconnected\n");
			rockchip_usb3phy_power_off(usb3phy_port->phy);
			usb3phy_port->suspended = true;
		}

		/*
		 * activate the linestate detection to get the next device
		 * plug-in irq.
		 */
		param_write(usb3phy->usb3phy_grf,
			    &usb3phy->cfgs->grfcfg.ls_det_st, true);
		param_write(usb3phy->usb3phy_grf,
			    &usb3phy->cfgs->grfcfg.ls_det_en, true);

		/*
		 * we don't need to rearm the delayed work when the phy port
		 * is suspended.
		 */
		mutex_unlock(&usb3phy_port->mutex);
		return;
	default:
		dev_dbg(&usb3phy_port->phy->dev, "unknown phy state\n");
		break;
	}

next_schedule:
	mutex_unlock(&usb3phy_port->mutex);
	schedule_delayed_work(&usb3phy_port->um_sm_work, SCHEDULE_DELAY);
}

static irqreturn_t rockchip_usb3phy_um_ls_irq(int irq, void *data)
{
	struct rockchip_usb3phy_port *usb3phy_port = data;
	struct rockchip_usb3phy *usb3phy =
		dev_get_drvdata(usb3phy_port->phy->dev.parent);

	if (!param_expect(usb3phy->usb3phy_grf,
			  &usb3phy->cfgs->grfcfg.ls_det_st,
			  usb3phy->cfgs->grfcfg.ls_det_st.dvalue))
		return IRQ_NONE;

	dev_dbg(usb3phy->dev, "utmi linestate interrupt\n");
	mutex_lock(&usb3phy_port->mutex);

	/* disable linestate detect irq and clear its status */
	param_write(usb3phy->usb3phy_grf, &usb3phy->cfgs->grfcfg.ls_det_en, false);
	param_write(usb3phy->usb3phy_grf, &usb3phy->cfgs->grfcfg.ls_det_st, true);

	mutex_unlock(&usb3phy_port->mutex);

	/*
	 * In this case for host phy, a new device is plugged in, meanwhile,
	 * if the phy port is suspended, we need rearm the work to resume it
	 * and mange its states; otherwise, we just return irq handled.
	 */
	if (usb3phy_port->suspended) {
		dev_dbg(usb3phy->dev, "schedule utmi sm work\n");
		rockchip_usb3phy_um_sm_work(&usb3phy_port->um_sm_work.work);
	}

	return IRQ_HANDLED;
}

static void rockchip_usb3phy_port_cleanup(void *data)
{
	struct rockchip_usb3phy_port *usb3phy_port = data;

printk("%s\n", __func__);
	if (usb3phy_port->provider)
		of_phy_provider_unregister(usb3phy_port->provider);

	if (usb3phy_port->rst_por)
		reset_control_put(usb3phy_port->rst_por);

	if (usb3phy_port->rst_apb)
		reset_control_put(usb3phy_port->rst_apb);

	if (usb3phy_port->rst_mac)
		reset_control_put(usb3phy_port->rst_mac);

	if (usb3phy_port->clk)
		clk_put(usb3phy_port->clk);
}

static int rockchip_usb3phy_port_init(struct rockchip_usb3phy *usb3phy,
				struct rockchip_usb3phy_port *usb3phy_port,
				struct device_node *child_np)
{
	struct platform_device *port_pdev;
	const struct of_device_id *match;
	struct device *dev, *phy_dev;
	struct resource *res;
	int ret;

	port_pdev = of_find_device_by_node(child_np);
	if (!port_pdev)
		return -ENODEV;

	dev = &port_pdev->dev;
	match = of_match_device(usb3phy->cfgs->ports, dev);
	if (!match) {
		dev_err(dev, "could not find port match data\n");
		return -EINVAL;
	}

	usb3phy_port->type = (enum rockchip_usb3phy_port_id)match->data;
	usb3phy_port->suspended = true; /* initial status */
	mutex_init(&usb3phy_port->mutex);

	usb3phy_port->phy = devm_phy_create(usb3phy->dev, child_np,
					    &rockchip_usb3phy_ops);
	if (IS_ERR(usb3phy_port->phy)) {
		dev_err(usb3phy->dev, "failed to create phy\n");
		return PTR_ERR(usb3phy_port->phy);
	}
	phy_set_drvdata(usb3phy_port->phy, usb3phy_port);
	phy_dev = &usb3phy_port->phy->dev;

	/* cleanup devres elements attached to the utmi/pipe devices */
	ret = devm_add_action(phy_dev,
			      rockchip_usb3phy_port_cleanup, usb3phy_port);
	if (ret)
		return ret;

	res = platform_get_resource(port_pdev, IORESOURCE_MEM, 0);
	usb3phy_port->base = devm_ioremap_resource(phy_dev, res);
	if (IS_ERR(usb3phy_port->base)) {
		dev_err(usb3phy->dev, "failed to remap phy regs\n");
		return PTR_ERR(usb3phy_port->base);
	}

	usb3phy_port->clk = clk_get(dev, "apb_pclk");
	if (IS_ERR(usb3phy_port->clk)) {
		dev_err(usb3phy->dev, "failed to get port clock\n");
		return PTR_ERR(usb3phy_port->clk);
	}

	usb3phy_port->rst_por = reset_control_get_optional(dev, "por");
	if (IS_ERR(usb3phy_port->rst_por))
		return PTR_ERR(usb3phy_port->rst_por);

	usb3phy_port->rst_apb = reset_control_get_optional(dev, "apb");
	if (IS_ERR(usb3phy_port->rst_apb))
		return PTR_ERR(usb3phy_port->rst_apb);

	usb3phy_port->rst_mac = reset_control_get_optional(dev, "mac");
	if (IS_ERR(usb3phy_port->rst_mac))
		return PTR_ERR(usb3phy_port->rst_mac);

	reset_control_assert(usb3phy_port->rst_por);
	reset_control_assert(usb3phy_port->rst_apb);
	reset_control_assert(usb3phy_port->rst_mac);

	if (usb3phy_port->type == USB3PHY_PORT_PIPE) {
		usb3phy_port->refclk_25m_quirk =
			of_property_read_bool(child_np,
					      "rockchip,refclk-25m-quirk");
	} else {
		usb3phy_port->type = USB3PHY_PORT_UTMI;
		INIT_DELAYED_WORK(&usb3phy_port->um_sm_work,
				  rockchip_usb3phy_um_sm_work);

		usb3phy->um_ls_irq = platform_get_irq_byname(port_pdev,
							     "linestate");
		if (usb3phy->um_ls_irq < 0) {
			dev_err(dev, "get utmi linestate irq failed\n");
			return -ENXIO;
		}

		ret = devm_request_threaded_irq(phy_dev,
						usb3phy->um_ls_irq, NULL,
						rockchip_usb3phy_um_ls_irq,
						IRQF_ONESHOT, "rockchip_usb3phy",
						usb3phy_port);
		if (ret) {
			dev_err(usb3phy->dev, "failed to request utmi linestate irq handle\n");
			return ret;
		}
	}

	ret = clk_prepare_enable(usb3phy_port->clk);
	if (ret)
		return ret;

	reset_control_deassert(usb3phy_port->rst_apb);
	reset_control_deassert(usb3phy_port->rst_por);
	reset_control_deassert(usb3phy_port->rst_mac);

	if (usb3phy->cfgs->phy_tuning) {
		ret = usb3phy->cfgs->phy_tuning(usb3phy, usb3phy_port, child_np);
		if (ret) {
			clk_disable_unprepare(usb3phy_port->clk);
			return ret;
		}
	}

	clk_disable_unprepare(usb3phy_port->clk);

	usb3phy_port->provider = of_phy_provider_register(dev,
							  of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(usb3phy_port->provider);
}

static int rockchip_usb3phy_on_init(struct usb_phy *usb_phy)
{
	struct rockchip_usb3phy *usb3phy =
		container_of(usb_phy, struct rockchip_usb3phy, usb_phy);
	int i;

	for (i = 0; i < USB3PHY_NUM_PORTS; i++) {
		struct rockchip_usb3phy_port *usb3phy_port = &usb3phy->ports[i];

		reset_control_deassert(usb3phy_port->rst_por);
		reset_control_deassert(usb3phy_port->rst_mac);
	}

	return 0;
}

static void rockchip_usb3phy_on_shutdown(struct usb_phy *usb_phy)
{
	struct rockchip_usb3phy *usb3phy =
		container_of(usb_phy, struct rockchip_usb3phy, usb_phy);
	int i;

	for (i = 0; i < USB3PHY_NUM_PORTS; i++) {
		struct rockchip_usb3phy_port *usb3phy_port = &usb3phy->ports[i];

		reset_control_assert(usb3phy_port->rst_por);
		reset_control_assert(usb3phy_port->rst_mac);
	}

	udelay(1);
}

static int rockchip_usb3phy_on_disconnect(struct usb_phy *usb_phy,
					enum usb_device_speed speed)
{
	struct rockchip_usb3phy *usb3phy =
		container_of(usb_phy, struct rockchip_usb3phy, usb_phy);

	dev_info(usb3phy->dev, "%s device has disconnected\n",
		 (speed == USB_SPEED_SUPER) ? "U3" : "UW/U2/U1.1/U1");

	if (speed == USB_SPEED_SUPER)
		atomic_notifier_call_chain(&usb_phy->notifier, 0, NULL);

	return 0;
}

static int rk3328_usb3phy_pipe_power(struct rockchip_usb3phy *usb3phy,
				   struct rockchip_usb3phy_port *usb3phy_port,
				   bool on)
{
	unsigned int reg;

	if (on) {
		reg = readl(usb3phy_port->base + 0x1a8);
		reg &= ~BIT(4); /* ldo power up */
		writel(reg, usb3phy_port->base + 0x1a8);

		reg = readl(usb3phy_port->base + 0x044);
		reg &= ~BIT(4); /* bg power on */
		writel(reg, usb3phy_port->base + 0x044);

		reg = readl(usb3phy_port->base + 0x150);
		reg |= BIT(6); /* tx bias enable */
		writel(reg, usb3phy_port->base + 0x150);

		reg = readl(usb3phy_port->base + 0x080);
		reg &= ~BIT(2); /* tx cm power up */
		writel(reg, usb3phy_port->base + 0x080);

		reg = readl(usb3phy_port->base + 0x0c0);
		/* tx obs enable and rx cm enable */
		reg |= (BIT(3) | BIT(4));
		writel(reg, usb3phy_port->base + 0x0c0);

		udelay(1);
	} else {
		reg = readl(usb3phy_port->base + 0x1a8);
		reg |= BIT(4); /* ldo power down */
		writel(reg, usb3phy_port->base + 0x1a8);

		reg = readl(usb3phy_port->base + 0x044);
		reg |= BIT(4); /* bg power down */
		writel(reg, usb3phy_port->base + 0x044);

		reg = readl(usb3phy_port->base + 0x150);
		reg &= ~BIT(6); /* tx bias disable */
		writel(reg, usb3phy_port->base + 0x150);

		reg = readl(usb3phy_port->base + 0x080);
		reg |= BIT(2); /* tx cm power down */
		writel(reg, usb3phy_port->base + 0x080);

		reg = readl(usb3phy_port->base + 0x0c0);
		/* tx obs disable and rx cm disable */
		reg &= ~(BIT(3) | BIT(4));
		writel(reg, usb3phy_port->base + 0x0c0);
	}

	return 0;
}

static int rk3328_usb3phy_tuning(struct rockchip_usb3phy *usb3phy,
			       struct rockchip_usb3phy_port *usb3phy_port,
			       struct device_node *child_np)
{
	if (usb3phy_port->type == USB3PHY_PORT_UTMI) {
		/*
		 * For rk3328 SoC, pre-emphasis and pre-emphasis strength must
		 * be written as one fixed value as below.
		 *
		 * Dissimilarly, the odt 45ohm value should be flexibly tuninged
		 * for the different boards to adjust HS eye height, so its
		 * value can be assigned in DT in code design.
		 */

		/* {bits[2:0]=111}: always enable pre-emphasis */
		usb3phy->apbcfg.u2_pre_emp = 0x0f;

		/* {bits[5:3]=000}: pre-emphasis strength as the weakest */
		usb3phy->apbcfg.u2_pre_emp_sth = 0x41;

		/* {bits[4:0]=10101}: odt 45ohm tuning */
		usb3phy->apbcfg.u2_odt_tuning = 0xb5;
		/* optional override of the odt 45ohm tuning */
		of_property_read_u32(child_np, "rockchip,odt-val-tuning",
				     &usb3phy->apbcfg.u2_odt_tuning);

		writel(usb3phy->apbcfg.u2_pre_emp, usb3phy_port->base + 0x030);
		writel(usb3phy->apbcfg.u2_pre_emp_sth, usb3phy_port->base + 0x040);
		writel(usb3phy->apbcfg.u2_odt_tuning, usb3phy_port->base + 0x11c);
	} else if (usb3phy_port->type == USB3PHY_PORT_PIPE) {
		if (usb3phy_port->refclk_25m_quirk) {
			dev_dbg(usb3phy->dev, "switch to 25m refclk\n");
			/* ref clk switch to 25M */
			writel(0x64, usb3phy_port->base + 0x11c);
			writel(0x64, usb3phy_port->base + 0x028);
			writel(0x01, usb3phy_port->base + 0x020);
			writel(0x21, usb3phy_port->base + 0x030);
			writel(0x06, usb3phy_port->base + 0x108);
			writel(0x00, usb3phy_port->base + 0x118);
		} else {
			/* configure for 24M ref clk */
			writel(0x80, usb3phy_port->base + 0x10c);
			writel(0x01, usb3phy_port->base + 0x118);
			writel(0x38, usb3phy_port->base + 0x11c);
			writel(0x83, usb3phy_port->base + 0x020);
			writel(0x02, usb3phy_port->base + 0x108);
		}

		/* Enable SSC */
		udelay(3);
		writel(0x08, usb3phy_port->base + 0x000);
		writel(0x0c, usb3phy_port->base + 0x120);

		/* Tuning Rx for compliance RJTL test */
		writel(0x70, usb3phy_port->base + 0x150);
		writel(0x12, usb3phy_port->base + 0x0c8);
		writel(0x05, usb3phy_port->base + 0x148);
		writel(0x08, usb3phy_port->base + 0x068);
		writel(0xf0, usb3phy_port->base + 0x1c4);
		writel(0xff, usb3phy_port->base + 0x070);
		writel(0x0f, usb3phy_port->base + 0x06c);
		writel(0xe0, usb3phy_port->base + 0x060);

		/*
		 * Tuning Tx to increase the bias current
		 * used in TX driver and RX EQ, it can
		 * also increase the voltage of LFPS.
		 */
		writel(0x08, usb3phy_port->base + 0x180);
	} else {
		dev_err(usb3phy->dev, "invalid usb3phy port type\n");
		return -EINVAL;
	}

	return 0;
}

static const struct of_device_id rk3328_usb3phy_ports[] = {
	{ .compatible = "rockchip,rk3328-usb3phy-utmi", .data = (void *)USB3PHY_PORT_UTMI },
	{ .compatible = "rockchip,rk3328-usb3phy-pipe", .data = (void *)USB3PHY_PORT_PIPE },
	{ /* sentinel */ }
};

static const struct rockchip_usb3phy_cfg rk3328_usb3phy_cfgs[] = {
	{
		.reg		= 0x0,
		.grfcfg		= {
			.um_suspend	= { 0x0004, 15, 0, 0x1452, 0x15d1 },
			.um_ls		= { 0x0030, 5, 4, 0, 1 },
			.um_hstdct	= { 0x0030, 7, 7, 0, 1 },
			.ls_det_en	= { 0x0040, 0, 0, 0, 1 },
			.ls_det_st	= { 0x0044, 0, 0, 0, 1 },
			.pp_pwr_st	= { 0x0034, 14, 13, 0, 0},
			.pp_pwr_en	= { {0x0020, 14, 0, 0x0014, 0x0005},
					    {0x0020, 14, 0, 0x0014, 0x000d},
					    {0x0020, 14, 0, 0x0014, 0x0015},
					    {0x0020, 14, 0, 0x0014, 0x001d} },
		},
		.ports		= rk3328_usb3phy_ports,
		.phy_pipe_power	= rk3328_usb3phy_pipe_power,
		.phy_tuning	= rk3328_usb3phy_tuning,
	},
	{ /* sentinel */ }
};

static int rockchip_usb3phy_probe(struct platform_device *pdev)
{
	const struct rockchip_usb3phy_cfg *phy_cfgs;
	const struct of_device_id *match;
	struct rockchip_usb3phy *usb3phy;
	struct device *dev = &pdev->dev;
	unsigned int reg;
	int i, ret;

	usb3phy = devm_kzalloc(dev, sizeof(*usb3phy), GFP_KERNEL);
	if (!usb3phy)
		return -ENOMEM;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "phy-cfgs are not assigned!\n");
		return -EINVAL;
	}
	phy_cfgs = match->data;

	if (!dev->parent || !dev->parent->of_node)
		return -EINVAL;

	usb3phy->usb3phy_grf = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(usb3phy->usb3phy_grf))
		return PTR_ERR(usb3phy->usb3phy_grf);

	if (of_property_read_u32(dev->of_node, "reg", &reg)) {
		dev_err(dev, "missing reg property\n");
		return -EINVAL;
	}

	usb3phy->dev = dev;
	platform_set_drvdata(pdev, usb3phy);

	/* find out a proper config which can be matched with dt. */
	i = 0;
	while (phy_cfgs[i].ports) {
		if (phy_cfgs[i].reg == reg) {
			usb3phy->cfgs = &phy_cfgs[i];
			break;
		}

		++i;
	}

	if (!usb3phy->cfgs) {
		dev_err(dev, "no phy-cfgs can be matched\n");
		return -EINVAL;
	}

	for (i = 0; i < USB3PHY_NUM_PORTS; i++) {
		struct rockchip_usb3phy_port *usb3phy_port = &usb3phy->ports[i];
		struct device_node *port = of_parse_phandle(dev->of_node, "ports", i);
		if (!port) {
			of_node_put(port);
			return -ENODEV;
		}

		ret = rockchip_usb3phy_port_init(usb3phy, usb3phy_port, port);
		of_node_put(port);
		if (ret) {
			dev_err(dev, "usb3phy port init failed, %d\n", ret);
			return ret;
		}
	}

	usb3phy->usb_phy.dev = dev;
	usb3phy->usb_phy.init = rockchip_usb3phy_on_init;
	usb3phy->usb_phy.shutdown = rockchip_usb3phy_on_shutdown;
	usb3phy->usb_phy.notify_disconnect = rockchip_usb3phy_on_disconnect;
	usb_add_phy(&usb3phy->usb_phy, USB_PHY_TYPE_USB3);
	ATOMIC_INIT_NOTIFIER_HEAD(&usb3phy->usb_phy.notifier);

	dev_info(dev, "Rockchip usb3phy initialized successfully\n");
	return 0;
}

static const struct of_device_id rockchip_usb3phy_dt_match[] = {
	{ .compatible = "rockchip,rk3328-usb3phy", .data = &rk3328_usb3phy_cfgs },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_usb3phy_dt_match);

static struct platform_driver rockchip_usb3phy_driver = {
	.probe		= rockchip_usb3phy_probe,
	.driver		= {
		.name	= "rockchip-usb3phy",
		.of_match_table = rockchip_usb3phy_dt_match,
	},
};
module_platform_driver(rockchip_usb3phy_driver);

MODULE_AUTHOR("Frank Wang <frank.wang@rock-chips.com>");
MODULE_AUTHOR("William Wu <william.wu@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USB 3.0 PHY driver");
MODULE_LICENSE("GPL v2");
