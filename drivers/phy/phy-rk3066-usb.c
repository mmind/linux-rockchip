/*
 * Rockchip rk3066 and rk3188 USB phy driver
 *
 * Copyright (C) 2014 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>


#define UOC_CON0			0x0
#define UOC_CON0_TXBITSTUFF_H		BIT(15)
#define UOC_CON0_TXBITSTUFF_L		BIT(14)
#define UOC_CON0_SIDDQ			BIT(13)
#define UOC_CON0_PORT_RESET		BIT(12)
#define UOC_CON0_REFCLK_MASK		(0x3 << 10)
#define UOC_CON0_REFCLK_CORE		(0x2 << 10)
#define UOC_CON0_REFCLK_XO		(0x1 << 10)
#define UOC_CON0_REFCLK_CRYSTAL		0
#define UOC_CON0_BYPASS			BIT(9) /* rk3188 and only phy0 */
#define UOC_CON0_BYPASS_DM		BIT(8) /* rk3188 and only phy0 */
#define UOC_CON0_REFCLK_DIV_MASK	(0x3 << 8) /* rk3066a */

#define UOC_CON0_OTG_TUNE_MASK		(0x7 << 5)
#define UOC_CON0_OTG_DISABLE		BIT(4)
#define UOC_CON0_COMPDIS_TUNE_MASK	(0x7 << 1)
#define UOC_CON0_SUSPEND_PD		BIT(0)

#define UOC_CON1			0x4
#define UOC_CON1_TXRISE_TUNE_MASK	(0x3 << 14)
#define UOC_CON1_TXHSXV_TUNE_MASK	(0x3 << 12)
#define UOC_CON1_TXVREF_TUNE_MASK	(0xf << 8)
#define UOC_CON1_TXFSLS_TUNE_MASK	(0xf << 4)
#define UOC_CON1_TXPREEMP_TUNE		BIT(3)
#define UOC_CON1_SQRXTUNE		(0x7 << 0)

#define UOC_CON2			0x8
#define UOC_CON2_ADP_PROBLE		BIT(15) /* rk3188 */
#define UOC_CON2_ADP_DISCHARGE		BIT(14) /* rk3188 */
#define UOC_CON2_ADP_CHARGE		BIT(13) /* rk3188 */
#define UOC_CON2_TXRES_TUNE_MASK	(0x3 << 11)  /* rk3188 */
#define UOC_CON2_SCALEDOWN_MASK		(0x3 << 11) /* rk3066a uoc0 */

#define UOC_CON2_SLEEP_MODE		BIT(10)

#define UOC_CON2_VREGTUNE		BIT(9) /* rk3066 */
#define UOC_CON2_UTMI_TERMSELECT	BIT(8) /* rk3066 */
#define UOC_CON2_UTMI_XCVRSELECT_MASK	(0x3 << 6) /* rk3066 */
#define UOC_CON2_UTMI_OPMODE_MASK	(0x3 << 4) /* rk3066 */
#define UOC_CON2_UTMI_SUSPEND_DISABLE	BIT(3) /* rk3066 */
#define UOC_CON2_RETENTION		BIT(8) /* rk3188 */
#define UOC_CON2_REFCLK_FREQ_MASK	(0x7 << 5) /* rk3188 */
#define UOC_CON2_TX_PREEMP_TUNE_MASK	(0x3 << 3) /* rk3188 */

#define UOC_CON2_SOFT_CTRL		BIT(2)
#define UOC_CON2_VBUS_VALID_EXTSEL	BIT(1)
#define UOC_CON2_VBUS_VALID_EXT		BIT(0)

/* only rk3188 and rk3066 uoc1 */
#define UOC_CON3			0xc
#define UOC_CON3_BVALID_INT_PEND	BIT(15) /* only phy0 */
#define UOC_CON3_BVALID_INT_ENABLE	BIT(14) /* only phy0 */
#define UOC_CON3_UTMI_TERMSELECT	BIT(5)
#define UOC_CON3_UTMI_XCVRSELECT_MASK	(0x3 << 3)
#define UOC_CON3_UTMI_OPMODE_MASK	(0x3 << 1)
#define UOC_CON3_UTMI_SUSPEND_DISABLE	BIT(0)

#define UOC_CON3_SCALEDOWN_MASK		(0x3 << 6) /* rk3066a uoc1 */



////////////////////////////

#define REG_ISCR			0x00
#define REG_PHYCTL			0x04
#define REG_PHYBIST			0x08
#define REG_PHYTUNE			0x0c

#define PHYCTL_DATA			BIT(7)

#define SUNXI_AHB_ICHR8_EN		BIT(10)
#define SUNXI_AHB_INCR4_BURST_EN	BIT(9)
#define SUNXI_AHB_INCRX_ALIGN_EN	BIT(8)
#define SUNXI_ULPI_BYPASS_EN		BIT(0)

/* Common Control Bits for Both PHYs */
#define PHY_PLL_BW			0x03
#define PHY_RES45_CAL_EN		0x0c

/* Private Control Bits for Each PHY */
#define PHY_TX_AMPLITUDE_TUNE		0x20
#define PHY_TX_SLEWRATE_TUNE		0x22
#define PHY_VBUSVALID_TH_SEL		0x25
#define PHY_PULLUP_RES_SEL		0x27
#define PHY_OTG_FUNC_EN			0x28
#define PHY_VBUS_DET_EN			0x29
#define PHY_DISCON_TH_SEL		0x2a

#define MAX_PHYS			3

struct sun4i_usb_phy_data {
	struct clk *clk;
	void __iomem *base;
	struct mutex mutex;
	int num_phys;
	u32 disc_thresh;
	struct sun4i_usb_phy {
		struct phy *phy;
		void __iomem *pmu;
		struct regulator *vbus;
		struct reset_control *reset;
		int index;
	} phys[MAX_PHYS];
};

#define to_sun4i_usb_phy_data(phy) \
	container_of((phy), struct sun4i_usb_phy_data, phys[(phy)->index])

static void sun4i_usb_phy_write(struct sun4i_usb_phy *phy, u32 addr, u32 data,
				int len)
{
	struct sun4i_usb_phy_data *phy_data = to_sun4i_usb_phy_data(phy);
	u32 temp, usbc_bit = BIT(phy->index * 2);
	int i;

	mutex_lock(&phy_data->mutex);

	for (i = 0; i < len; i++) {
		temp = readl(phy_data->base + REG_PHYCTL);

		/* clear the address portion */
		temp &= ~(0xff << 8);

		/* set the address */
		temp |= ((addr + i) << 8);
		writel(temp, phy_data->base + REG_PHYCTL);

		/* set the data bit and clear usbc bit*/
		temp = readb(phy_data->base + REG_PHYCTL);
		if (data & 0x1)
			temp |= PHYCTL_DATA;
		else
			temp &= ~PHYCTL_DATA;
		temp &= ~usbc_bit;
		writeb(temp, phy_data->base + REG_PHYCTL);

		/* pulse usbc_bit */
		temp = readb(phy_data->base + REG_PHYCTL);
		temp |= usbc_bit;
		writeb(temp, phy_data->base + REG_PHYCTL);

		temp = readb(phy_data->base + REG_PHYCTL);
		temp &= ~usbc_bit;
		writeb(temp, phy_data->base + REG_PHYCTL);

		data >>= 1;
	}
	mutex_unlock(&phy_data->mutex);
}

static void sun4i_usb_phy_passby(struct sun4i_usb_phy *phy, int enable)
{
	u32 bits, reg_value;

	if (!phy->pmu)
		return;

	bits = SUNXI_AHB_ICHR8_EN | SUNXI_AHB_INCR4_BURST_EN |
		SUNXI_AHB_INCRX_ALIGN_EN | SUNXI_ULPI_BYPASS_EN;

	reg_value = readl(phy->pmu);

	if (enable)
		reg_value |= bits;
	else
		reg_value &= ~bits;

	writel(reg_value, phy->pmu);
}

static int sun4i_usb_phy_init(struct phy *_phy)
{
	struct sun4i_usb_phy *phy = phy_get_drvdata(_phy);
	struct sun4i_usb_phy_data *data = to_sun4i_usb_phy_data(phy);
	int ret;

	ret = clk_prepare_enable(data->clk);
	if (ret)
		return ret;

	ret = reset_control_deassert(phy->reset);
	if (ret) {
		clk_disable_unprepare(data->clk);
		return ret;
	}

	/* Adjust PHY's magnitude and rate */
	sun4i_usb_phy_write(phy, PHY_TX_AMPLITUDE_TUNE, 0x14, 5);

	/* Disconnect threshold adjustment */
	sun4i_usb_phy_write(phy, PHY_DISCON_TH_SEL, data->disc_thresh, 2);

	sun4i_usb_phy_passby(phy, 1);

	return 0;
}

static int sun4i_usb_phy_exit(struct phy *_phy)
{
	struct sun4i_usb_phy *phy = phy_get_drvdata(_phy);
	struct sun4i_usb_phy_data *data = to_sun4i_usb_phy_data(phy);

	sun4i_usb_phy_passby(phy, 0);
	reset_control_assert(phy->reset);
	clk_disable_unprepare(data->clk);

	return 0;
}

static int sun4i_usb_phy_power_on(struct phy *_phy)
{
	struct sun4i_usb_phy *phy = phy_get_drvdata(_phy);
	int ret = 0;

	if (phy->vbus)
		ret = regulator_enable(phy->vbus);

	return ret;
}

static int sun4i_usb_phy_power_off(struct phy *_phy)
{
	struct sun4i_usb_phy *phy = phy_get_drvdata(_phy);

	if (phy->vbus)
		regulator_disable(phy->vbus);

	return 0;
}

static struct phy_ops sun4i_usb_phy_ops = {
	.init		= sun4i_usb_phy_init,
	.exit		= sun4i_usb_phy_exit,
	.power_on	= sun4i_usb_phy_power_on,
	.power_off	= sun4i_usb_phy_power_off,
	.owner		= THIS_MODULE,
};

static struct phy *sun4i_usb_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	struct sun4i_usb_phy_data *data = dev_get_drvdata(dev);

	if (WARN_ON(args->args[0] == 0 || args->args[0] >= data->num_phys))
		return ERR_PTR(-ENODEV);

	return data->phys[args->args[0]].phy;
}

static int sun4i_usb_phy_probe(struct platform_device *pdev)
{
	struct sun4i_usb_phy_data *data;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *pmu = NULL;
	struct phy_provider *phy_provider;
	struct reset_control *reset;
	struct regulator *vbus;
	struct resource *res;
	struct phy *phy;
	char name[16];
	int i;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->mutex);

	if (of_device_is_compatible(np, "allwinner,sun5i-a13-usb-phy"))
		data->num_phys = 2;
	else
		data->num_phys = 3;

	if (of_device_is_compatible(np, "allwinner,sun4i-a10-usb-phy"))
		data->disc_thresh = 3;
	else
		data->disc_thresh = 2;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_ctrl");
	data->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	data->clk = devm_clk_get(dev, "usb_phy");
	if (IS_ERR(data->clk)) {
		dev_err(dev, "could not get usb_phy clock\n");
		return PTR_ERR(data->clk);
	}

	/* Skip 0, 0 is the phy for otg which is not yet supported. */
	for (i = 1; i < data->num_phys; i++) {
		snprintf(name, sizeof(name), "usb%d_vbus", i);
		vbus = devm_regulator_get_optional(dev, name);
		if (IS_ERR(vbus)) {
			if (PTR_ERR(vbus) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			vbus = NULL;
		}

		snprintf(name, sizeof(name), "usb%d_reset", i);
		reset = devm_reset_control_get(dev, name);
		if (IS_ERR(reset)) {
			dev_err(dev, "failed to get reset %s\n", name);
			return PTR_ERR(reset);
		}

		if (i) { /* No pmu for usbc0 */
			snprintf(name, sizeof(name), "pmu%d", i);
			res = platform_get_resource_byname(pdev,
							IORESOURCE_MEM, name);
			pmu = devm_ioremap_resource(dev, res);
			if (IS_ERR(pmu))
				return PTR_ERR(pmu);
		}

		phy = devm_phy_create(dev, &sun4i_usb_phy_ops, NULL);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create PHY %d\n", i);
			return PTR_ERR(phy);
		}

		data->phys[i].phy = phy;
		data->phys[i].pmu = pmu;
		data->phys[i].vbus = vbus;
		data->phys[i].reset = reset;
		data->phys[i].index = i;
		phy_set_drvdata(phy, &data->phys[i]);
	}

	dev_set_drvdata(dev, data);
	phy_provider = devm_of_phy_provider_register(dev, sun4i_usb_phy_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	return 0;
}

static const struct of_device_id sun4i_usb_phy_of_match[] = {
	{ .compatible = "allwinner,sun4i-a10-usb-phy" },
	{ .compatible = "allwinner,sun5i-a13-usb-phy" },
	{ .compatible = "allwinner,sun7i-a20-usb-phy" },
	{ },
};
MODULE_DEVICE_TABLE(of, sun4i_usb_phy_of_match);

static struct platform_driver sun4i_usb_phy_driver = {
	.probe	= sun4i_usb_phy_probe,
	.driver = {
		.of_match_table	= sun4i_usb_phy_of_match,
		.name  = "sun4i-usb-phy",
		.owner = THIS_MODULE,
	}
};
module_platform_driver(sun4i_usb_phy_driver);

MODULE_DESCRIPTION("Allwinner sun4i USB phy driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL v2");
