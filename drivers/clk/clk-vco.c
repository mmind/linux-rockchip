// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Heiko Stuebner <heiko@sntech.de>
 *
 * Generic voltage controlled oscillator
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

struct clk_vco {
	struct device *dev;
	struct clk_hw hw;
	u32 rate;
	struct regulator *supply;
	struct gpio_desc *enable_gpio;
};

#define to_clk_vco(_hw) container_of(_hw, struct clk_vco, hw)

static int clk_vco_prepare(struct clk_hw *hw)
{
	return regulator_enable(to_clk_vco(hw)->supply);
}

static void clk_vco_unprepare(struct clk_hw *hw)
{
	regulator_disable(to_clk_vco(hw)->supply);
}

static int clk_vco_enable(struct clk_hw *hw)
{
	gpiod_set_value(to_clk_vco(hw)->enable_gpio, 1);
	return 0;
}

static void clk_vco_disable(struct clk_hw *hw)
{
	gpiod_set_value(to_clk_vco(hw)->enable_gpio, 0);
}

static unsigned long clk_vco_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	return to_clk_vco(hw)->rate;
}

const struct clk_ops clk_vco_ops = {
	.prepare = clk_vco_prepare,
	.unprepare = clk_vco_unprepare,
	.enable = clk_vco_enable,
	.disable = clk_vco_disable,
	.recalc_rate = clk_vco_recalc_rate,
};

static int clk_vco_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_vco *clkgen;
	const char *clk_name;
	int ret;

	clkgen = devm_kzalloc(dev, sizeof(*clkgen), GFP_KERNEL);
	if (!clkgen)
		return -ENOMEM;

	clkgen->dev = dev;

	if (device_property_read_u32(dev, "clock-frequency", &clkgen->rate))
		return dev_err_probe(dev, -EIO, "failed to get clock-frequency");

	ret = device_property_read_string(dev, "clock-output-names", &clk_name);
	if (ret)
		clk_name = fwnode_get_name(dev->fwnode);

	clkgen->supply = devm_regulator_get_optional(dev, "vdd");
	if (IS_ERR(clkgen->supply)) {
		if (PTR_ERR(clkgen->supply) != -ENODEV)
			return dev_err_probe(dev, PTR_ERR(clkgen->supply),
					     "failed to get regulator\n");
		clkgen->supply = NULL;
	}

	clkgen->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						      GPIOD_OUT_LOW);
	if (IS_ERR(clkgen->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(clkgen->enable_gpio),
				     "failed to get gpio\n");

	ret = gpiod_direction_output(clkgen->enable_gpio, 0);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to set gpio output");

	clkgen->hw.init = CLK_HW_INIT_NO_PARENT(clk_name, &clk_vco_ops, 0);

	/* register the clock */
	ret = devm_clk_hw_register(dev, &clkgen->hw);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register clock\n");

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					  &clkgen->hw);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register clock provider\n");

	return 0;
}

static const struct of_device_id clk_vco_ids[] = {
	{ .compatible = "voltage-oscillator" },
	{ }
};
MODULE_DEVICE_TABLE(of, clk_vco_ids);

static struct platform_driver clk_vco_driver = {
	.driver = {
		.name = "clk_vco",
		.of_match_table = clk_vco_ids,
	},
	.probe = clk_vco_probe,
};
module_platform_driver(clk_vco_driver);

MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("Voltage controlled oscillator driver");
MODULE_LICENSE("GPL");
