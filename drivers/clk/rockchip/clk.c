/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * based on
 *
 * samsung/clk.c
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Copyright (c) 2013 Linaro Ltd.
 * Author: Thomas Abraham <thomas.ab@samsung.com>
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

#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include "clk.h"

static DEFINE_SPINLOCK(clk_lock);
static struct clk **clk_table;
static void __iomem *reg_base;
static struct clk_onecell_data clk_data;

void __init rockchip_clk_init(struct device_node *np, void __iomem *base,
			      unsigned long nr_clks)
{
	reg_base = base;

	clk_table = kzalloc(sizeof(struct clk *) * nr_clks, GFP_KERNEL);
	if (!clk_table)
		pr_err("%s: could not allocate clock lookup table\n", __func__);

	if (!np)
		return;

#ifdef CONFIG_OF
	clk_data.clks = clk_table;
	clk_data.clk_num = nr_clks;
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
#endif
}

void rockchip_clk_add_lookup(struct clk *clk, unsigned int id)
{
	if (clk_table && id)
		clk_table[id] = clk;
}

void __init rockchip_clk_register_plls(struct rockchip_pll_clock *list,
				unsigned int nr_pll, void __iomem *reg_lock)
{
	struct clk *clk;
	int idx;

	for (idx = 0; idx < nr_pll; idx++, list++) {
		clk = rockchip_clk_register_pll(list->type, list->name,
				list->parent_names, list->num_parents,
				reg_base, list->con_offset, reg_lock,
				list->lock_shift, list->mode_offset,
				list->mode_shift, list->rate_table, &clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		rockchip_clk_add_lookup(clk, list->id);
	}
}

void __init rockchip_clk_register_armclk(unsigned int lookup_id,
			const char *name, const char **parent_names,
			unsigned int num_parents, void __iomem *reg_base,
			struct device_node *np)
{
	struct clk *clk;

	clk = rockchip_clk_register_cpuclk(name, parent_names, num_parents,
					   reg_base, np, &clk_lock);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register clock %s\n", __func__,
			name);
		return;
	}

	rockchip_clk_add_lookup(clk, lookup_id);
}

void __init rockchip_clk_register_mux(struct rockchip_mux_clock *list,
				      unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int idx;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		clk = clk_register_mux(NULL, list->name, list->parent_names,
			list->num_parents, list->flags, reg_base + list->offset,
			list->shift, list->width, list->mux_flags, &clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		rockchip_clk_add_lookup(clk, list->id);
	}
}

void __init rockchip_clk_register_div(struct rockchip_div_clock *list,
					unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int idx;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		if (list->table)
			clk = clk_register_divider_table(NULL, list->name,
					list->parent_name, list->flags,
					reg_base + list->offset, list->shift,
					list->width, list->div_flags,
					list->table, &clk_lock);
		else
			clk = clk_register_divider(NULL, list->name,
					list->parent_name, list->flags,
					reg_base + list->offset, list->shift,
					list->width, list->div_flags,
					&clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		rockchip_clk_add_lookup(clk, list->id);
	}
}

void __init rockchip_clk_register_gate(struct rockchip_gate_clock *list,
			       unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int idx;
	unsigned long flags;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		flags = list->flags | CLK_SET_RATE_PARENT;

		/* keep all gates untouched for now */
		flags |= CLK_IGNORE_UNUSED;

		clk = clk_register_gate(NULL, list->name, list->parent_name,
				flags, reg_base + list->offset,
				list->bit_idx, list->gate_flags, &clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		rockchip_clk_add_lookup(clk, list->id);
	}
}

void __init rockchip_clk_init_from_table(struct rockchip_clk_init_table *tbl,
					 unsigned int nr_tbl)
{
	struct clk *clk;
	unsigned int idx;

	for (idx = 0; idx < nr_tbl; idx++, tbl++) {
		clk = __clk_lookup(tbl->name);
		if (!clk) {
			pr_err("%s: Failed to find clock %s\n",
			       __func__, tbl->name);
			continue;
		}

		if (tbl->parent_name) {
			struct clk *parent = __clk_lookup(tbl->parent_name);
			pr_info("%s: setting parent of %s to %s\n", __func__, tbl->name, tbl->parent_name);
			if (clk_set_parent(clk, parent)) {
				pr_err("%s: Failed to set parent %s of %s\n",
				       __func__, tbl->parent_name, tbl->name);
				WARN_ON(1);
			}
		}

		if (tbl->rate) {
			pr_info("%s: setting rate of %s to %lu\n", __func__, tbl->name, tbl->rate);
			if (clk_set_rate(clk, tbl->rate)) {
				pr_err("%s: Failed to set rate %lu of %s\n",
				       __func__, tbl->rate, tbl->name);
				WARN_ON(1);
			}
		}

		if (tbl->state) {
			pr_info("%s: enabling %s\n", __func__, tbl->name);
			if (clk_prepare_enable(clk)) {
				pr_err("%s: Failed to enable %s\n", __func__,
				       tbl->name);
				WARN_ON(1);
			}
		}
	}
}

rockchip_clk_apply_init_table_func rockchip_clk_apply_init_table;

void __init rockchip_clocks_apply_init_table(void)
{
	if (!rockchip_clk_apply_init_table)
		return;

	pr_info("%s: applying initial clock settings\n", __func__);
	rockchip_clk_apply_init_table();
}
