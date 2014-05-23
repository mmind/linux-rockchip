/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * based on clk/samsung/clk-cpu.c
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file contains the utility functions to register the cpu clocks
 * for rockchip platforms.
 */

#include <linux/of.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include "clk.h"

/**
 * struct samsung_cpuclk: information about clock supplied to a CPU core.
 * @hw:		handle between ccf and cpu clock.
 * @alt_parent:	alternate parent clock to use when switching the speed
 *		of the primary parent clock.
 * @reg:	base register for cpu-clock values.
 * @clk_nb:	clock notifier registered for changes in clock speed of the
 *		primary parent clock.
 * @data:	optional data which the acutal instantiation of this clock
 *		can use.
 */
struct rockchip_cpuclk {
	struct clk_hw		hw;
	struct clk		*alt_parent;
	void __iomem		*reg_base;
	struct notifier_block	clk_nb;
	void			*data;
	spinlock_t		*lock;
};

#define to_rockchip_cpuclk_hw(hw) container_of(hw, struct rockchip_cpuclk, hw)
#define to_rockchip_cpuclk_nb(nb) \
			container_of(nb, struct rockchip_cpuclk, clk_nb)

/**
 * struct rockchip_cpuclk_soc_data: soc specific data for cpu clocks.
 * @parser:	pointer to a function that can parse SoC specific data.
 * @ops:	clock operations to be used for this clock.
 * @clk_cb:	the clock notifier callback to be called for changes in the
 *		clock rate of the primary parent clock.
 *
 * This structure provides SoC specific data for ARM clocks. Based on
 * the compatible value of the clock controller node, the value of the
 * fields in this structure can be populated.
 */
struct rockchip_cpuclk_soc_data {
	int			(*parser)(struct device_node *, void **);
	const struct clk_ops	*ops;
	int (*clk_cb)(struct notifier_block *nb, unsigned long evt, void *data);
};

static unsigned long _calc_div(unsigned long prate, unsigned long drate)
{
	unsigned long div = prate / drate;
	return (!(prate % drate)) ? div-- : div;
}

/**
 * struct rk2928_cpuclk_data: config data for rk2928/rk3066/rk3188 cpu clocks.
 * @prate:	frequency of the parent clock.
 * @clksel0:	value to be programmed in the clksel0 register.
 * @clksel1:	value to be programmed in the clksel1 register.
 *
 * This structure holds the divider configuration data for for core clocks
 * directly depending on the cpu clockspeed. The parent frequency at which
 * these values are vaild is specified in @prate.
 */
struct rk2928_cpuclk_data {
	unsigned long		prate;
	u32			clksel0;
	u32			clksel1;
};

/**
 * struct rk2928_reg_data: describes register offsets and masks of the cpuclock
 * @div_core_shift:	core divider offset used to divider the pll value
 * @div_core_mask:	core divider mask
 * @mux_core_shift:	offset of the core multiplexer
 */
struct rk2928_reg_data {
	u8		div_core_shift;
	u32		div_core_mask;
	u8		mux_core_shift;
};

/*
 * This clock notifier is called when the frequency of the parent clock
 * of cpuclk is to be changed. This notifier handles the setting up all
 * the divider clocks, remux to temporary parent and handling the safe
 * frequency levels when using temporary parent.
 */
static int rk2928_cpuclk_common_notifier_cb(struct notifier_block *nb,
					unsigned long event, void *data,
					const struct rk2928_reg_data *reg_data)
{
	struct clk_notifier_data *ndata = data;
	struct rockchip_cpuclk *cpuclk = to_rockchip_cpuclk_nb(nb);
	struct rk2928_cpuclk_data *cpuclk_data = cpuclk->data;
	unsigned long alt_prate, alt_div;

	if (!cpuclk_data)
		return NOTIFY_BAD;

	switch (event) {
	case PRE_RATE_CHANGE:
		alt_prate = clk_get_rate(cpuclk->alt_parent);

		/* pre-rate change. find out the divider values */
		while (cpuclk_data->prate != ndata->new_rate) {
			if (cpuclk_data->prate == 0)
				return NOTIFY_BAD;
			cpuclk_data++;
		}

		/*
		 * if the new and old parent clock speed is less than the
		 * clock speed of the alternate parent, then it should be
		 * ensured that at no point the cpuclk speed is more than
		 * the old_prate until the dividers are set.
		 */
		if (ndata->old_rate < alt_prate &&
					ndata->new_rate < alt_prate) {
			alt_div = _calc_div(alt_prate, ndata->old_rate);
			writel_relaxed(HIWORD_UPDATE(alt_div,
						     reg_data->div_core_mask,
						     reg_data->div_core_shift),
				      cpuclk->reg_base + RK2928_CLKSEL_CON(0));
		}

		/* mux to alternate parent */
		writel(HIWORD_UPDATE(1, 1, reg_data->mux_core_shift),
		       cpuclk->reg_base + RK2928_CLKSEL_CON(0));

		/* set new divider values for depending clocks */
		writel(cpuclk_data->clksel0,
		       cpuclk->reg_base + RK2928_CLKSEL_CON(0));
		writel_relaxed(cpuclk_data->clksel1,
		       cpuclk->reg_base + RK2928_CLKSEL_CON(1));
		break;
	case POST_RATE_CHANGE:
		/* post-rate change event, re-mux back to primary parent */
		writel(HIWORD_UPDATE(0, 1, reg_data->mux_core_shift),
		       cpuclk->reg_base + RK2928_CLKSEL_CON(0));

		/* remove any core dividers */
		writel(HIWORD_UPDATE(0, reg_data->div_core_mask,
				     reg_data->div_core_shift),
		       cpuclk->reg_base + RK2928_CLKSEL_CON(0));
		break;
	}

	return NOTIFY_OK;
}

#define RK2928_DIV_CORE_SHIFT		0
#define RK2928_DIV_CORE_MASK		0x1f
#define RK2928_MUX_CORE_SHIFT		7

static unsigned long rockchip_rk2928_cpuclk_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct rockchip_cpuclk *cpuclk = to_rockchip_cpuclk_hw(hw);
	u32 clksel0 = readl_relaxed(cpuclk->reg_base + RK2928_CLKSEL_CON(0));

	clksel0 >>= RK2928_DIV_CORE_SHIFT;
	clksel0 &= RK2928_DIV_CORE_MASK;
	return parent_rate / (clksel0 + 1);
}

static const struct clk_ops rk2928_cpuclk_ops = {
	.recalc_rate = rockchip_rk2928_cpuclk_recalc_rate,
};

static const struct rk2928_reg_data rk2928_data = {
	.div_core_shift = RK2928_DIV_CORE_SHIFT,
	.div_core_mask = RK2928_DIV_CORE_MASK,
	.mux_core_shift = RK2928_MUX_CORE_SHIFT,
};

static int rk2928_cpuclk_notifier_cb(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	return rk2928_cpuclk_common_notifier_cb(nb, event, data, &rk2928_data);
}

static const struct rockchip_cpuclk_soc_data rk2928_cpuclk_soc_data = {
	.ops = &rk2928_cpuclk_ops,
	.clk_cb = rk2928_cpuclk_notifier_cb,
};

#define RK3066_MUX_CORE_SHIFT		8

static const struct rk2928_reg_data rk3066_data = {
	.div_core_shift = RK2928_DIV_CORE_SHIFT,
	.div_core_mask = RK2928_DIV_CORE_MASK,
	.mux_core_shift = RK3066_MUX_CORE_SHIFT,
};

static int rk3066_cpuclk_notifier_cb(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	return rk2928_cpuclk_common_notifier_cb(nb, event, data, &rk3066_data);
}

static const struct rockchip_cpuclk_soc_data rk3066_cpuclk_soc_data = {
	.ops = &rk2928_cpuclk_ops,
	.clk_cb = rk3066_cpuclk_notifier_cb,
};

#define RK3188_DIV_CORE_SHIFT		9
#define RK3188_DIV_CORE_MASK		0x1f
#define RK3188_MUX_CORE_SHIFT		8
#define RK3188_DIV_CORE_PERIPH_MASK	0x3
#define RK3188_DIV_CORE_PERIPH_SHIFT	6
#define RK3188_DIV_ACLK_CORE_MASK	0x7
#define RK3188_DIV_ACLK_CORE_SHIFT	3

#define RK3188_CLKSEL0(div_core_periph) \
		HIWORD_UPDATE(div_core_periph, RK3188_DIV_CORE_PERIPH_MASK,\
				 RK3188_DIV_CORE_PERIPH_SHIFT)
#define RK3188_CLKSEL1(div_aclk_core) \
		HIWORD_UPDATE(div_aclk_core, RK3188_DIV_ACLK_CORE_MASK,\
				 RK3188_DIV_ACLK_CORE_SHIFT)

static unsigned long rockchip_rk3188_cpuclk_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct rockchip_cpuclk *cpuclk = to_rockchip_cpuclk_hw(hw);
	u32 clksel0 = readl_relaxed(cpuclk->reg_base + RK2928_CLKSEL_CON(0));

	clksel0 >>= RK3188_DIV_CORE_SHIFT;
	clksel0 &= RK3188_DIV_CORE_MASK;
	return parent_rate / (clksel0 + 1);
}

static const struct clk_ops rk3188_cpuclk_ops = {
	.recalc_rate = rockchip_rk3188_cpuclk_recalc_rate,
};

static const struct rk2928_reg_data rk3188_data = {
	.div_core_shift = RK3188_DIV_CORE_SHIFT,
	.div_core_mask = RK3188_DIV_CORE_MASK,
	.mux_core_shift = RK3188_MUX_CORE_SHIFT,
};

static int rk3188_cpuclk_notifier_cb(struct notifier_block *nb,
				unsigned long event, void *data)
{
	return rk2928_cpuclk_common_notifier_cb(nb, event, data, &rk3188_data);
}

/*
 * parse divider configuration data from dt for all the cpu clock domain
 * clocks in rk3188 and compatible SoC's.
 */
static int __init rk3188_cpuclk_parser(struct device_node *np, void **data)
{
	struct rk2928_cpuclk_data *tdata;
	int proplen, ret, num_rows, i, col;
	u32 cfg[10], cells;

	ret = of_property_read_u32(np, "#rockchip,armclk-cells", &cells);
	if (ret)
		return -EINVAL;

	proplen = of_property_count_u32_elems(np,
					      "rockchip,armclk-divider-table");
	if (proplen < 0)
		return proplen;
	if (!proplen || proplen % cells)
		return -EINVAL;

	num_rows = proplen / cells;

	*data = kzalloc(sizeof(*tdata) * (num_rows + 1), GFP_KERNEL);
	if (!*data)
		return -ENOMEM;

	tdata = *data;

	for (i = 0; i < num_rows; i++, tdata++) {
		for (col = 0; col < cells; col++) {
			ret = of_property_read_u32_index(np,
					"rockchip,armclk-divider-table",
					i * cells + col, &cfg[col]);
			if (ret) {
				pr_err("%s: failed to read col %d in row %d of %d\n",
				       __func__, col, i, num_rows);
				kfree(*data);
				return ret;
			}
		}

		tdata->prate = cfg[0] * 1000;
		tdata->clksel0 = RK3188_CLKSEL0(cfg[1]);
		tdata->clksel1 = RK3188_CLKSEL1(cfg[2]);
	}
	tdata->prate = 0;
	return 0;
}

static const struct rockchip_cpuclk_soc_data rk3188_cpuclk_soc_data = {
	.parser = rk3188_cpuclk_parser,
	.ops = &rk3188_cpuclk_ops,
	.clk_cb = rk3188_cpuclk_notifier_cb,
};

static const struct of_device_id rockchip_clock_ids_cpuclk[] = {
	{ .compatible = "rockchip,rk2928-cru",
			.data = &rk2928_cpuclk_soc_data, },
	{ .compatible = "rockchip,rk3066-cru",
			.data = &rk3066_cpuclk_soc_data, },
	{ .compatible = "rockchip,rk3188-cru",
			.data = &rk3188_cpuclk_soc_data, },
	{ /* sentinel */ },
};

/**
 * rockchip_clk_register_cpuclk: register arm clock with ccf.
 * @lookup_id: cpuclk clock output id for the clock controller.
 * @parent_names: names of the parent clocks for cpuclk.
 * @num_parents: number of parent clocks
 * @base: base address of the clock controller from which cpuclk is generated.
 * @np: device tree node pointer of the clock controller.
 * @ops: clock ops for this clock (optional)
 */
struct clk *rockchip_clk_register_cpuclk(const char *name,
			const char **parent_names, unsigned int num_parents,
			void __iomem *reg_base, struct device_node *np,
			spinlock_t *lock)
{
	struct rockchip_cpuclk *cpuclk;
	struct clk_init_data init;
	const struct rockchip_cpuclk_soc_data *soc_data;
	const struct of_device_id *match;
	struct clk *clk;
	int ret;

	if (!np) {
		pr_err("%s: missing device node\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	match = of_match_node(rockchip_clock_ids_cpuclk, np);
	if (!match) {
		pr_err("%s: no matching cpuclock found\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	soc_data = match->data;

	if (!soc_data->clk_cb)
		return ERR_PTR(-EINVAL);

	if (num_parents != 2) {
		pr_err("%s: missing alternative parent clock\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	cpuclk = kzalloc(sizeof(*cpuclk), GFP_KERNEL);
	if (!cpuclk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = parent_names;
	init.num_parents = 1;
	init.ops = soc_data->ops;

	cpuclk->hw.init = &init;
	cpuclk->reg_base = reg_base;
	cpuclk->lock = lock;

	cpuclk->alt_parent = __clk_lookup(parent_names[1]);
	if (!cpuclk->alt_parent) {
		pr_err("%s: could not lookup alternate parent\n",
		       __func__);
		ret = -EINVAL;
		goto free_cpuclk;
	}

	ret = clk_prepare_enable(cpuclk->alt_parent);
	if (ret) {
		pr_err("%s: could not enable alternate parent\n",
		       __func__);
		goto free_cpuclk;
	}

	if (soc_data->parser) {
		ret = soc_data->parser(np, &cpuclk->data);
		if (ret) {
			pr_err("%s: error %d in parsing %s clock data",
					__func__, ret, name);
			ret = -EINVAL;
			goto free_cpuclk;
		}
	}

	cpuclk->clk_nb.notifier_call = soc_data->clk_cb;
	if (clk_notifier_register(__clk_lookup(parent_names[0]),
			&cpuclk->clk_nb)) {
		pr_err("%s: failed to register clock notifier for %s\n",
				__func__, name);
		goto free_cpuclk_data;
	}

	clk = clk_register(NULL, &cpuclk->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: could not register cpuclk %s\n", __func__,	name);
		ret = PTR_ERR(clk);
		goto free_cpuclk_data;
	}

	return clk;

free_cpuclk_data:
	kfree(cpuclk->data);
free_cpuclk:
	kfree(cpuclk);
	return ERR_PTR(ret);
}
