/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
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

#include <asm/div64.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/clk-private.h>
#include "clk.h"

struct rockchip_clk_pll {
	struct clk_hw		hw;
	void __iomem		*reg_base;
	void __iomem		*reg_mode;
	unsigned int		mode_shift;
	void __iomem		*reg_lock;
	unsigned int		lock_shift;
	enum rockchip_pll_type	type;
	const struct rockchip_pll_rate_table *rate_table;
	unsigned int		rate_count;
	spinlock_t		*lock;
};

#define to_clk_pll(_hw) container_of(_hw, struct rockchip_clk_pll, hw)

static const struct rockchip_pll_rate_table *rockchip_get_pll_settings(
			    struct rockchip_clk_pll *pll, unsigned long rate)
{
	const struct rockchip_pll_rate_table  *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++) {
		if (rate == rate_table[i].rate)
			return &rate_table[i];
	}

	return NULL;
}

static long rockchip_pll_round_rate(struct clk_hw *hw,
			    unsigned long drate, unsigned long *prate)
{
	struct rockchip_clk_pll *pll = to_clk_pll(hw);
	const struct rockchip_pll_rate_table *rate_table = pll->rate_table;
	int i;

	/*
	 * For anything smaller or equal to the parent rate, we can only
	 * bypass the pll, so the parent_rate is the lowest we can get.
	 */
	if (drate <= *prate)
		return *prate;

	/* Assumming rate_table is in descending order */
	for (i = 0; i < pll->rate_count; i++) {
		if (drate >= rate_table[i].rate)
			return rate_table[i].rate;
	}

	/* return minimum supported value */
	return rate_table[i - 1].rate;
}

static int rockchip_pll_wait_lock(struct rockchip_clk_pll *pll)
{
	int delay = 24000000;

	while (delay > 0) {
		if (readl(pll->reg_lock) & BIT(pll->lock_shift))
			return 0;
		delay--;
	}

	pr_err("%s: timeout waiting for pll to lock\n", __func__);
	return -ETIMEDOUT;
}

/**
 * PLL used in RK3066 and RK3188
 */

#define RK3066_PLL_MODE_MASK		0x3
#define RK3066_PLL_MODE_SLOW		0x0
#define RK3066_PLL_MODE_NORM		0x1
#define RK3066_PLL_MODE_DEEP		0x2

#define RK3066_PLL_RESET_DELAY(nr)	((nr * 500) / 24 + 1)

#define RK3066_PLLCON(i)		(i * 0x4)

#define RK3066_PLLCON0_OD_MASK		0xf
#define RK3066_PLLCON0_OD_SHIFT		0
#define RK3066_PLLCON0_NR_MASK		0x3f
#define RK3066_PLLCON0_NR_SHIFT		8

#define RK3066_PLLCON1_NF_MASK		0x1fff
#define RK3066_PLLCON1_NF_SHIFT		0

#define RK3066_PLLCON2_BWADJ_MASK	0xfff
#define RK3066_PLLCON2_BWADJ_SHIFT	0

#define RK3066_PLLCON3_RESET		(1 << 5)
#define RK3066_PLLCON3_PWRDOWN		(1 << 1)
#define RK3066_PLLCON3_BYPASS		(1 << 0)

static unsigned long rockchip_rk3066_pll_recalc_rate(struct clk_hw *hw,
						     unsigned long prate)
{
	struct rockchip_clk_pll *pll = to_clk_pll(hw);
	u64 nf, nr, no, rate64 = prate;
	u32 pllcon;

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(3));
	if (pllcon & RK3066_PLLCON3_BYPASS) {
		pr_debug("%s: pll %s is bypassed\n", __func__,
			__clk_get_name(hw->clk));
		return prate;
	}

	pllcon = readl_relaxed(pll->reg_mode) >> pll->mode_shift;
	pllcon &= RK3066_PLL_MODE_MASK;
	switch (pllcon) {
	case RK3066_PLL_MODE_NORM:
		/* calculate the PLL rate below */
		break;
	case RK3066_PLL_MODE_SLOW:
		return prate;
	case RK3066_PLL_MODE_DEEP:
		pr_warn("%s: deep slow mode for pll %s currently unknown, assuming parent rate\n",
			__func__, __clk_get_name(hw->clk));
		return prate;
	}

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(1));
	nf = (pllcon >> RK3066_PLLCON1_NF_SHIFT) & RK3066_PLLCON1_NF_MASK;

	pllcon = readl_relaxed(pll->reg_base + RK3066_PLLCON(0));
	nr = (pllcon >> RK3066_PLLCON0_NR_SHIFT) & RK3066_PLLCON0_NR_MASK;
	no = (pllcon >> RK3066_PLLCON0_OD_SHIFT) & RK3066_PLLCON0_OD_MASK;

	rate64 *= (nf + 1);
	do_div(rate64, nr + 1);
	do_div(rate64, no + 1);

	return (unsigned long)rate64;
}

static int rockchip_rk3066_pll_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long prate)
{
	struct rockchip_clk_pll *pll = to_clk_pll(hw);
	const struct rockchip_pll_rate_table *rate;
	unsigned long old_rate = rockchip_rk3066_pll_recalc_rate(hw, prate);
	int ret;

	pr_debug("%s: changing %s from %lu to %lu with a parent rate of %lu\n",
		 __func__, __clk_get_name(hw->clk), old_rate, drate, prate);

	if (drate == prate) {
		writel(HIWORD_UPDATE(RK3066_PLL_MODE_SLOW, RK3066_PLL_MODE_MASK,
				     pll->mode_shift),
		       pll->reg_mode);

		/* powerdown the pll, as it is unused */
		writel(HIWORD_UPDATE(RK3066_PLLCON3_PWRDOWN,
				     RK3066_PLLCON3_PWRDOWN, 0),
		       pll->reg_base + RK3066_PLLCON(3));

		return 0;
	}

	/* need to powerup the pll first */
	if (old_rate == prate)
		writel(HIWORD_UPDATE(0, RK3066_PLLCON3_PWRDOWN, 0),
		       pll->reg_base + RK3066_PLLCON(3));

	/* Get required rate settings from table */
	rate = rockchip_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	pr_debug("%s: rate settings for %lu (nr, no, nf): (%d, %d, %d)\n",
		 __func__, rate->rate, rate->nr, rate->no, rate->nf);

	/* put pll in slow mode and enter reset afterwards */
	writel(HIWORD_UPDATE(RK3066_PLL_MODE_SLOW, RK3066_PLL_MODE_MASK,
					pll->mode_shift), pll->reg_mode);
	writel(HIWORD_UPDATE(RK3066_PLLCON3_RESET, RK3066_PLLCON3_RESET, 0),
					pll->reg_base + RK3066_PLLCON(3));

	/* update pll values */
	writel(HIWORD_UPDATE(rate->nr - 1, RK3066_PLLCON0_NR_MASK,
					   RK3066_PLLCON0_NR_SHIFT) |
	       HIWORD_UPDATE(rate->no - 1, RK3066_PLLCON0_OD_MASK,
					   RK3066_PLLCON0_OD_SHIFT),
	       pll->reg_base + RK3066_PLLCON(0));

	writel_relaxed(HIWORD_UPDATE(rate->nf - 1, RK3066_PLLCON1_NF_MASK,
						   RK3066_PLLCON1_NF_SHIFT),
		       pll->reg_base + RK3066_PLLCON(1));
	writel_relaxed(HIWORD_UPDATE(rate->bwadj,RK3066_PLLCON2_BWADJ_MASK,
						 RK3066_PLLCON2_BWADJ_SHIFT),
		       pll->reg_base + RK3066_PLLCON(2));

	/* leave reset and wait the reset_delay */
	writel(HIWORD_UPDATE(0, RK3066_PLLCON3_RESET, 0),
	       pll->reg_base + RK3066_PLLCON(3));
	udelay(RK3066_PLL_RESET_DELAY(rate->nr));

	/* wait for the pll to lock */
	ret = rockchip_pll_wait_lock(pll);
	if (ret) {
		pr_warn("%s: trying to restore old rate %lu\n",
			__func__, old_rate);
		rockchip_rk3066_pll_set_rate(hw, old_rate, prate);
	}

	/* go back to normal mode */
	writel(HIWORD_UPDATE(RK3066_PLL_MODE_NORM, RK3066_PLL_MODE_MASK,
			     pll->mode_shift), pll->reg_mode);

	return ret;
}

static const struct clk_ops rockchip_rk3066_pll_clk_ro_ops = {
	.recalc_rate = rockchip_rk3066_pll_recalc_rate,
};

static const struct clk_ops rockchip_rk3066_pll_clk_ops = {
	.recalc_rate = rockchip_rk3066_pll_recalc_rate,
	.round_rate = rockchip_pll_round_rate,
	.set_rate = rockchip_rk3066_pll_set_rate,
};

struct clk *rockchip_clk_register_pll(struct rockchip_pll_clock *pll_clk,
				void __iomem *base, void __iomem *reg_lock,
				spinlock_t *lock)
{
	struct rockchip_clk_pll *pll;
	struct clk *clk;
	struct clk_init_data init;
	int len;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: could not allocate pll clk %s\n",
			__func__, pll_clk->name);
		return ERR_PTR(-ENOMEM);
	}

	init.name = pll_clk->name;
	init.flags = pll_clk->flags;
	init.parent_names = &pll_clk->parent_name;
	init.num_parents = 1;

	if (pll_clk->rate_table) {
		/* find count of rates in rate_table */
		for (len = 0; pll_clk->rate_table[len].rate != 0; )
			len++;

		pll->rate_count = len;
		pll->rate_table = kmemdup(pll_clk->rate_table,
					pll->rate_count *
					sizeof(struct rockchip_pll_rate_table),
					GFP_KERNEL);
		WARN(!pll->rate_table,
			"%s: could not allocate rate table for %s\n",
			__func__, pll_clk->name);
	}

	switch (pll_clk->type) {
	case pll_rk3066:
		if (!pll->rate_table)
			init.ops = &rockchip_rk3066_pll_clk_ro_ops;
		else
			init.ops = &rockchip_rk3066_pll_clk_ops;
		break;
	default:
		pr_warn("%s: Unknown pll type for pll clk %s\n",
			__func__, pll_clk->name);
	}

	pll->hw.init = &init;
	pll->type = pll_clk->type;
	pll->reg_base = base + pll_clk->con_offset;
	pll->reg_mode = base + pll_clk->mode_offset;
	pll->mode_shift = pll_clk->mode_shift;
	pll->reg_lock = reg_lock;
	pll->lock_shift = pll_clk->lock_shift;
	pll->lock = lock;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll clock %s : %ld\n",
			__func__, pll_clk->name, PTR_ERR(clk));
		kfree(pll);
	}

	return clk;
}
