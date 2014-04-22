/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * based on
 *
 * samsung/clk.h
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

#ifndef CLK_ROCKCHIP_CLK_H
#define CLK_ROCKCHIP_CLK_H

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

/* register positions shared by RK2928, RK3066 and RK3188 */
#define RK2928_PLL_CON(x)		(x * 0x4)
#define RK2928_MODE_CON		0x40
#define RK2928_CLKSEL_CON(x)	(x * 0x4 + 0x44)
#define RK2928_CLKGATE_CON(x)	(x * 0x4 + 0xd0)
#define RK2928_GLB_SRST_FST		0x100
#define RK2928_GLB_SRST_SND		0x104
#define RK2928_SOFTRST_CON(x)	(x * 0x4 + 0x110)

enum rockchip_pll_type {
	pll_rk3066,
};

#define RK3066_PLL_RATE(_rate, _nr, _nf, _no)	\
{						\
	.rate	= _rate##U,			\
	.nr = _nr,				\
	.nf = _nf,				\
	.no = _no,				\
	.bwadj = (_nf >> 1),			\
}

#define RK3188_PLL_RATE(_rate, _nr, _nf, _no)	\
{						\
	.rate	= _rate##U,			\
	.nr = _nr,				\
	.nf = _nf,				\
	.no = _no,				\
	.bwadj = 0,				\
}

struct rockchip_pll_rate_table {
	unsigned long rate;
	unsigned int nr;
	unsigned int nf;
	unsigned int no;
	unsigned int bwadj;
};

/**
 * struct rockchip_pll_clock: information about pll clock
 * @id: platform specific id of the clock.
 * @name: name of this pll clock.
 * @parent_name: name of the parent clock.
 * @flags: optional flags for basic clock.
 * @con_offset: offset of the register for configuring the PLL.
 * @mode_offset: offset of the register for configuring the PLL-mode.
 * @mode_shift: offset inside the mode-register for the mode of this pll.
 * @lock_shift: offset inside the lock register for the lock status.
 * @type: Type of PLL to be registered.
 * @rate_table: Table of usable pll rates
 */
struct rockchip_pll_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	unsigned long		flags;
	int			con_offset;
	int			mode_offset;
	int			mode_shift;
	int			lock_shift;
	enum rockchip_pll_type	type;
	const struct rockchip_pll_rate_table *rate_table;
};

#define PLL(_type, _id, _name, _pname, _flags, _con, _mode, _mshift,	\
		_lshift, _rtable)					\
	{								\
		.id		= _id,					\
		.type		= _type,				\
		.name		= _name,				\
		.parent_name	= _pname,				\
		.flags		= CLK_GET_RATE_NOCACHE | _flags,	\
		.con_offset	= _con,					\
		.mode_offset	= _mode,				\
		.mode_shift	= _mshift,				\
		.lock_shift	= _lshift,				\
		.rate_table	= _rtable,				\
	}

struct clk *rockchip_clk_register_pll(struct rockchip_pll_clock *pll_clk,
				void __iomem *base, void __iomem *reg_lock,
				spinlock_t *lock);

struct clk *rockchip_clk_register_cpuclk(const char *name,
			const char **parent_names, unsigned int num_parents,
			void __iomem *reg_base, struct device_node *np,
			spinlock_t *lock);

#define PNAME(x) static const char *x[] __initconst

/**
 * struct rockchip_mux_clock: information about mux clock
 * @id: platform specific id of the clock.
 * @name: name of this mux clock.
 * @parent_names: array of pointer to parent clock names.
 * @num_parents: number of parents listed in @parent_names.
 * @flags: optional flags for basic clock.
 * @offset: offset of the register for configuring the mux.
 * @shift: starting bit location of the mux control bit-field in @reg.
 * @width: width of the mux control bit-field in @reg.
 * @mux_flags: flags for mux-type clock.
 */
struct rockchip_mux_clock {
	unsigned int		id;
	const char		*name;
	const char		**parent_names;
	u8			num_parents;
	unsigned long		flags;
	unsigned long		offset;
	u8			shift;
	u8			width;
	u8			mux_flags;
};

#define MUX(_id, cname, pnames, o, s, w, f, mf)			\
	{							\
		.id		= _id,				\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.offset		= o,				\
		.shift		= s,				\
		.width		= w,				\
		.mux_flags	= mf,				\
	}

/**
 * struct rockchip_div_clock: information about div clock
 * @id: platform specific id of the clock.
 * @name: name of this div clock.
 * @parent_name: name of the parent clock.
 * @flags: optional flags for basic clock.
 * @offset: offset of the register for configuring the div.
 * @shift: starting bit location of the div control bit-field in @reg.
 * @div_flags: flags for div-type clock.
 */
struct rockchip_div_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	unsigned long		flags;
	unsigned long		offset;
	u8			shift;
	u8			width;
	u8			div_flags;
	struct clk_div_table	*table;
};

#define DIV(_id, cname, pname, o, s, w, f, df, t)		\
	{							\
		.id		= _id,				\
		.name		= cname,			\
		.parent_name	= pname,			\
		.flags		= f,				\
		.offset		= o,				\
		.shift		= s,				\
		.width		= w,				\
		.div_flags	= df,				\
		.table		= t,				\
	}


/**
 * struct rockchip_gate_clock: information about gate clock
 * @id: platform specific id of the clock.
 * @name: name of this gate clock.
 * @parent_name: name of the parent clock.
 * @flags: optional flags for basic clock.
 * @offset: offset of the register for configuring the gate.
 * @bit_idx: bit index of the gate control bit-field in @reg.
 * @gate_flags: flags for gate-type clock.
 */
struct rockchip_gate_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	unsigned long		flags;
	unsigned long		offset;
	u8			bit_idx;
	u8			gate_flags;
};

#define GATE(_id, cname, pname, o, b, f, gf)			\
	{							\
		.id		= _id,				\
		.name		= cname,			\
		.parent_name	= pname,			\
		.flags		= f,				\
		.offset		= o,				\
		.bit_idx	= b,				\
		.gate_flags	= gf,				\
	}

void rockchip_clk_init(struct device_node *np, void __iomem *base,
		       unsigned long nr_clks);

void rockchip_clk_add_lookup(struct clk *clk, unsigned int id);

void rockchip_clk_register_mux(struct rockchip_mux_clock *clk_list,
			       unsigned int nr_clk);
void rockchip_clk_register_div(struct rockchip_div_clock *clk_list,
			       unsigned int nr_clk);
void rockchip_clk_register_gate(struct rockchip_gate_clock *clk_list,
				unsigned int nr_clk);
void rockchip_clk_register_plls(struct rockchip_pll_clock *pll_list,
				unsigned int nr_pll, void __iomem *reg_lock);
void rockchip_clk_register_armclk(unsigned int lookup_id,
			const char *name, const char **parent_names,
			unsigned int num_parents, void __iomem *reg_base,
			struct device_node *np);

#define ROCKCHIP_SOFTRST_HIWORD_MASK	BIT(0)

#ifdef CONFIG_RESET_CONTROLLER
void rockchip_register_softrst(struct device_node *np,
			       unsigned int num_regs,
			       void __iomem *base, u8 flags);
#else
static inline void rockchip_register_softrst(struct device_node *np,
			       unsigned int num_regs,
			       void __iomem *base, u8 flags)
{
}
#endif

/**
 * struct rockchip_clk_init_table - clock initialization table
 * @name:	clock name to set
 * @parent_name:parent clock name
 * @rate:	rate to set
 * @state:	enable/disable
 */
struct rockchip_clk_init_table {
	const char	*name;
	const char	*parent_name;
	unsigned long	rate;
	int		state;
};

void rockchip_clk_init_from_table(struct rockchip_clk_init_table *tbl,
				  unsigned int nr_tbl);

typedef void (*rockchip_clk_apply_init_table_func)(void);
extern rockchip_clk_apply_init_table_func rockchip_clk_apply_init_table;

#endif
