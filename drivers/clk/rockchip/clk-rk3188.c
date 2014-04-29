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

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <dt-bindings/clock/rk3188-cru.h>

#include "clk.h"

/* list of PLLs to be registered */
enum rk3188_plls {
	apll, cpll, dpll, gpll,
};

struct rockchip_pll_rate_table rk3188_apll_rates[] = {
	RK3188_PLL_RATE(2208000000, 1, 92, 1),
	RK3188_PLL_RATE(2184000000, 1, 91, 1),
	RK3188_PLL_RATE(2160000000, 1, 90, 1),
	RK3188_PLL_RATE(2136000000, 1, 89, 1),
	RK3188_PLL_RATE(2112000000, 1, 88, 1),
	RK3188_PLL_RATE(2088000000, 1, 87, 1),
	RK3188_PLL_RATE(2064000000, 1, 86, 1),
	RK3188_PLL_RATE(2040000000, 1, 85, 1),
	RK3188_PLL_RATE(2016000000, 1, 84, 1),
	RK3188_PLL_RATE(1992000000, 1, 83, 1),
	RK3188_PLL_RATE(1968000000, 1, 82, 1),
	RK3188_PLL_RATE(1944000000, 1, 81, 1),
	RK3188_PLL_RATE(1920000000, 1, 80, 1),
	RK3188_PLL_RATE(1896000000, 1, 79, 1),
	RK3188_PLL_RATE(1872000000, 1, 78, 1),
	RK3188_PLL_RATE(1848000000, 1, 77, 1),
	RK3188_PLL_RATE(1824000000, 1, 76, 1),
	RK3188_PLL_RATE(1800000000, 1, 75, 1),
	RK3188_PLL_RATE(1776000000, 1, 74, 1),
	RK3188_PLL_RATE(1752000000, 1, 73, 1),
	RK3188_PLL_RATE(1728000000, 1, 72, 1),
	RK3188_PLL_RATE(1704000000, 1, 71, 1),
	RK3188_PLL_RATE(1680000000, 1, 70, 1),
	RK3188_PLL_RATE(1656000000, 1, 69, 1),
	RK3188_PLL_RATE(1632000000, 1, 68, 1),
	RK3188_PLL_RATE(1608000000, 1, 67, 1),
	RK3188_PLL_RATE(1560000000, 1, 65, 1),
	RK3188_PLL_RATE(1512000000, 1, 63, 1),
	RK3188_PLL_RATE(1488000000, 1, 62, 1),
	RK3188_PLL_RATE(1464000000, 1, 61, 1),
	RK3188_PLL_RATE(1440000000, 1, 60, 1),
	RK3188_PLL_RATE(1416000000, 1, 59, 1),
	RK3188_PLL_RATE(1392000000, 1, 58, 1),
	RK3188_PLL_RATE(1368000000, 1, 57, 1),
	RK3188_PLL_RATE(1344000000, 1, 56, 1),
	RK3188_PLL_RATE(1320000000, 1, 55, 1),
	RK3188_PLL_RATE(1296000000, 1, 54, 1),
	RK3188_PLL_RATE(1272000000, 1, 53, 1),
	RK3188_PLL_RATE(1248000000, 1, 52, 1),
	RK3188_PLL_RATE(1224000000, 1, 51, 1),
	RK3188_PLL_RATE(1200000000, 1, 50, 1),
	RK3188_PLL_RATE(1176000000, 1, 49, 1),
	RK3188_PLL_RATE(1128000000, 1, 47, 1),
	RK3188_PLL_RATE(1104000000, 1, 46, 1),
	RK3188_PLL_RATE(1008000000, 1, 84, 2),
	RK3188_PLL_RATE( 912000000, 1, 76, 2),
	RK3188_PLL_RATE( 888000000, 1, 74, 2),
	RK3188_PLL_RATE( 816000000, 1, 68, 2),
	RK3188_PLL_RATE( 792000000, 1, 66, 2),
	RK3188_PLL_RATE( 696000000, 1, 58, 2),
	RK3188_PLL_RATE( 600000000, 1, 50, 2),
	RK3188_PLL_RATE( 504000000, 1, 84, 4),
	RK3188_PLL_RATE( 408000000, 1, 68, 4),
	RK3188_PLL_RATE( 312000000, 1, 52, 4),
	RK3188_PLL_RATE( 252000000, 1, 84, 8),
	RK3188_PLL_RATE( 216000000, 1, 72, 8),
	RK3188_PLL_RATE( 126000000, 1, 84, 16),
	RK3188_PLL_RATE(  48000000, 1, 64, 32),
	{ /* sentinel */ },
};

struct rockchip_pll_rate_table rk3188_cpll_rates[] = {
	RK3066_PLL_RATE(1188000000, 2,  99, 1),
	RK3066_PLL_RATE( 798000000, 2, 133, 2),
	RK3066_PLL_RATE( 768000000, 1,  64, 2),
	RK3066_PLL_RATE( 742500000, 8, 495, 2),
	RK3066_PLL_RATE( 600000000, 1,  50, 2),
	RK3066_PLL_RATE( 594000000, 2, 198, 4),
	RK3066_PLL_RATE( 552000000, 1,  46, 2),
	RK3066_PLL_RATE( 504000000, 1,  84, 4),
	RK3066_PLL_RATE( 456000000, 1,  76, 4),
	RK3066_PLL_RATE( 408000000, 1,  68, 4),
	RK3066_PLL_RATE( 360000000, 1,  60, 4),
	{ /* sentinel */ },
};

struct rockchip_pll_rate_table rk3188_gpll_rates[] = {
	RK3066_PLL_RATE(1200000000, 1,  50, 1),
	RK3066_PLL_RATE(1188000000, 2,  99, 1),
	RK3066_PLL_RATE( 891000000, 8, 594, 2),
	RK3066_PLL_RATE( 768000000, 1,  64, 2),
	RK3066_PLL_RATE( 594000000, 2, 198, 4),
	RK3066_PLL_RATE( 384000000, 2, 128, 4),
	RK3066_PLL_RATE( 300000000, 1,  50, 4),
	RK3066_PLL_RATE( 297000000, 2, 198, 8),
	RK3066_PLL_RATE( 148500000, 2,  99, 8),
	{ /* sentinel */ },
};

static struct rockchip_pll_clock rk3188_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3066, 0, "apll", "xin24m", 0, RK2928_PLL_CON(0),
		     RK2928_MODE_CON, 0, 6, rk3188_apll_rates),
	[dpll] = PLL(pll_rk3066, 0, "dpll", "xin24m", 0, RK2928_PLL_CON(4),
		     RK2928_MODE_CON, 4, 5, NULL),
	[cpll] = PLL(pll_rk3066, 0, "cpll", "xin24m", 0, RK2928_PLL_CON(8),
		     RK2928_MODE_CON, 8, 7, rk3188_cpll_rates),
	[gpll] = PLL(pll_rk3066, 0, "gpll", "xin24m", 0, RK2928_PLL_CON(12),
		     RK2928_MODE_CON, 12, 8, rk3188_gpll_rates),
};

PNAME(mux_pll_src_gpll_cpll_p)	= { "gpll", "cpll" };
PNAME(mux_pll_src_cpll_gpll_p)	= { "cpll", "gpll" };

PNAME(mux_armclk_p)		= { "apll", "gate_gpll_cpu" };
PNAME(mux_aclk_cpu_p)		= { "apll", "gpll" };

/* dummy is an undocumented clock called clk12m */
PNAME(mux_sclk_i2s_p)		= { "gate_div_i2s", "gate_frac_i2s", "dummy" };
PNAME(mux_sclk_spdif_p)		= { "gate_div_spdif", "gate_frac_spdif", "dummy" };

PNAME(mux_sclk_uart0_p)		= { "gate_div_uart0", "gate_frac_uart0", "xin24m" };
PNAME(mux_sclk_uart1_p)		= { "gate_div_uart1", "gate_frac_uart1", "xin24m" };
PNAME(mux_sclk_uart2_p)		= { "gate_div_uart2", "gate_frac_uart2", "xin24m" };
PNAME(mux_sclk_uart3_p)		= { "gate_div_uart3", "gate_frac_uart3", "xin24m" };

PNAME(mux_mac_p)		= { "gpll", "dpll" };
PNAME(mux_sclk_mac_p)		= { "gate_div_mac", "rmii_clkin" };

PNAME(mux_hsicphy_p)		= { "gate_otgphy0", "gate_otgphy1", "gpll", "cpll" };

PNAME(mux_ddr_p)		= { "dpll", "gate_gpll_ddr" };

#define MFLAGS CLK_MUX_HIWORD_MASK
static struct rockchip_mux_clock rk3188_mux_clks[] __initdata = {
	MUX(0, "mux_aclk_cpu", mux_aclk_cpu_p, RK2928_CLKSEL_CON(0), 5, 1, CLK_SET_RATE_NO_REPARENT, MFLAGS),
	MUX(0, "mux_aclk_peri", mux_pll_src_cpll_gpll_p, RK2928_CLKSEL_CON(10), 15, 1, CLK_SET_RATE_NO_REPARENT, MFLAGS),


	MUX(0, "mux_i2s_pll", mux_pll_src_gpll_cpll_p, RK2928_CLKSEL_CON(2), 15, 1, 0, MFLAGS),
	MUX(0, "mux_uart_pll", mux_pll_src_gpll_cpll_p, RK2928_CLKSEL_CON(12), 15, 1, 0, MFLAGS),

	MUX(0, "mux_sclk_i2s", mux_sclk_i2s_p, RK2928_CLKSEL_CON(3), 8, 2, 0, MFLAGS),
	MUX(0, "mux_sclk_spdif", mux_sclk_spdif_p, RK2928_CLKSEL_CON(5), 8, 2, 0, MFLAGS),

	MUX(SCLK_UART0, "mux_sclk_uart0", mux_sclk_uart0_p, RK2928_CLKSEL_CON(13), 8, 2, 0, MFLAGS),
	MUX(SCLK_UART1, "mux_sclk_uart1", mux_sclk_uart1_p, RK2928_CLKSEL_CON(14), 8, 2, 0, MFLAGS),
	MUX(SCLK_UART2, "mux_sclk_uart2", mux_sclk_uart2_p, RK2928_CLKSEL_CON(15), 8, 2, 0, MFLAGS),
	MUX(SCLK_UART3, "mux_sclk_uart3", mux_sclk_uart3_p, RK2928_CLKSEL_CON(16), 8, 2, 0, MFLAGS),

	MUX(0, "mux_hsicphy", mux_hsicphy_p, RK2928_CLKSEL_CON(30), 0, 2, 0, MFLAGS),

	MUX(0, "mux_mac", mux_mac_p, RK2928_CLKSEL_CON(21), 0, 2, 0, MFLAGS),
	MUX(SCLK_MAC, "mux_sclk_mac", mux_sclk_mac_p, RK2928_CLKSEL_CON(21), 4, 1, 0, MFLAGS),

	MUX(0, "mux_ddr", mux_ddr_p, RK2928_CLKSEL_CON(26), 8, 1, 0, MFLAGS),

	MUX(0, "mux_dclk_lcdc0", mux_pll_src_cpll_gpll_p, RK2928_CLKSEL_CON(27), 0, 1, 0, MFLAGS),
	MUX(0, "mux_dclk_lcdc1", mux_pll_src_cpll_gpll_p, RK2928_CLKSEL_CON(28), 0, 1, 0, MFLAGS),

	MUX(0, "mux_aclk_lcdc0_pre", mux_pll_src_cpll_gpll_p, RK2928_CLKSEL_CON(31), 7, 1, 0, MFLAGS),
	MUX(0, "mux_aclk_lcdc1_pre", mux_pll_src_cpll_gpll_p, RK2928_CLKSEL_CON(31), 15, 1, 0, MFLAGS),
	MUX(0, "mux_aclk_vepu", mux_pll_src_cpll_gpll_p, RK2928_CLKSEL_CON(32), 7, 1, 0, MFLAGS),
	MUX(0, "mux_aclk_vdpu", mux_pll_src_cpll_gpll_p, RK2928_CLKSEL_CON(32), 15, 1, 0, MFLAGS),
	MUX(0, "mux_aclk_gpu", mux_pll_src_cpll_gpll_p, RK2928_CLKSEL_CON(34), 7, 1, 0, MFLAGS),
};

/* 2 ^ (val + 1) */
static struct clk_div_table div_core_peri_t[] = {
	{ .val = 0, .div = 2 },
	{ .val = 1, .div = 4 },
	{ .val = 2, .div = 8 },
	{ .val = 3, .div = 16 },
	{ /* sentinel */},
};

static struct clk_div_table div_aclk_core_t[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 3 },
	{ .val = 3, .div = 4 },
	{ .val = 4, .div = 8 },
	{ /* sentinel */},
};

#define DFLAGS CLK_DIVIDER_HIWORD_MASK
static struct rockchip_div_clock rk3188_div_clks[] __initdata = {
	/* these two are set by the cpuclk and should not be changed */
	DIV(0, "div_core_peri", "armclk", RK2928_CLKSEL_CON(0), 6, 2, 0, DFLAGS | CLK_DIVIDER_READ_ONLY, div_core_peri_t),
	DIV(0, "div_aclk_core", "armclk", RK2928_CLKSEL_CON(1), 3, 3, 0, DFLAGS | CLK_DIVIDER_READ_ONLY, div_aclk_core_t),

	DIV(0, "div_aclk_cpu", "mux_aclk_cpu", RK2928_CLKSEL_CON(0), 0, 5, 0, DFLAGS, NULL),
	DIV(0, "div_hclk_cpu", "gate_aclk_cpu", RK2928_CLKSEL_CON(1), 8, 2, 0, DFLAGS | CLK_DIVIDER_POWER_OF_TWO, NULL),
	DIV(0, "div_pclk_cpu", "gate_aclk_cpu", RK2928_CLKSEL_CON(1), 12, 2, 0, DFLAGS | CLK_DIVIDER_POWER_OF_TWO, NULL),
	DIV(0, "div_hclk_ahb2apb", "gate_hclk_ahb2apb", RK2928_CLKSEL_CON(1), 14, 2, 0, DFLAGS | CLK_DIVIDER_POWER_OF_TWO, NULL),

	DIV(0, "div_i2s", "mux_i2s_pll", RK2928_CLKSEL_CON(3), 0, 5, 0, DFLAGS, NULL),
	DIV(0, "div_spdif", "mux_i2s_pll", RK2928_CLKSEL_CON(5), 0, 5, 0, DFLAGS, NULL),

	DIV(0, "div_aclk_peri", "mux_aclk_peri", RK2928_CLKSEL_CON(10), 0, 5, 0, DFLAGS, NULL),
	DIV(0, "div_hclk_peri", "gate_aclk_peri", RK2928_CLKSEL_CON(10), 8, 2, 0, DFLAGS | CLK_DIVIDER_POWER_OF_TWO, NULL),
	DIV(0, "div_pclk_peri", "gate_aclk_peri", RK2928_CLKSEL_CON(10), 12, 2, 0, DFLAGS | CLK_DIVIDER_POWER_OF_TWO, NULL),

	DIV(0, "div_uart0", "mux_uart_pll", RK2928_CLKSEL_CON(13), 0, 7, 0, DFLAGS, NULL),
	DIV(0, "div_uart1", "mux_uart_pll", RK2928_CLKSEL_CON(14), 0, 7, 0, DFLAGS, NULL),
	DIV(0, "div_uart2", "mux_uart_pll", RK2928_CLKSEL_CON(15), 0, 7, 0, DFLAGS, NULL),
	DIV(0, "div_uart3", "mux_uart_pll", RK2928_CLKSEL_CON(16), 0, 7, 0, DFLAGS, NULL),

	DIV(0, "div_mmc0", "gate_hclk_peri", RK2928_CLKSEL_CON(11), 0, 6, 0, DFLAGS, NULL),
	DIV(0, "div_mmc1", "gate_hclk_peri", RK2928_CLKSEL_CON(12), 0, 6, 0, DFLAGS, NULL),
	DIV(0, "div_mmc2", "gate_hclk_peri", RK2928_CLKSEL_CON(12), 8, 6, 0, DFLAGS, NULL),

	DIV(0, "div_mac", "mux_mac", RK2928_CLKSEL_CON(21), 8, 5, CLK_SET_RATE_PARENT, DFLAGS, NULL),

	DIV(0, "div_hsicphy", "mux_hsicphy", RK2928_CLKGATE_CON(11), 8, 6, 0, DFLAGS, NULL),

	DIV(0, "div_saradc", "xin24m", RK2928_CLKSEL_CON(24), 8, 8, 0, DFLAGS, NULL),

	DIV(0, "div_spi0", "gate_pclk_peri", RK2928_CLKSEL_CON(25), 0, 7, 0, DFLAGS, NULL),
	DIV(0, "div_spi1", "gate_pclk_peri", RK2928_CLKSEL_CON(25), 8, 7, 0, DFLAGS, NULL),

	DIV(0, "div_ddr", "mux_ddr", RK2928_CLKSEL_CON(26), 0, 2, 0, DFLAGS | CLK_DIVIDER_POWER_OF_TWO, NULL),

	DIV(0, "div_dclk_lcdc0", "mux_dclk_lcdc0", RK2928_CLKSEL_CON(27), 8, 8, 0, DFLAGS, NULL),
	DIV(0, "div_dclk_lcdc1", "mux_dclk_lcdc1", RK2928_CLKSEL_CON(28), 8, 8, 0, DFLAGS, NULL),

	DIV(0, "div_aclk_lcdc0_pre", "mux_aclk_lcdc0_pre", RK2928_CLKSEL_CON(31), 0, 5, 0, DFLAGS, NULL),
	DIV(0, "div_aclk_lcdc1_pre", "mux_aclk_lcdc1_pre", RK2928_CLKSEL_CON(31), 8, 5, 0, DFLAGS, NULL),
	DIV(0, "div_aclk_vepu", "mux_aclk_vepu", RK2928_CLKSEL_CON(32), 0, 5, 0, DFLAGS, NULL),
	DIV(0, "div_aclk_vdpu", "mux_aclk_vdpu", RK2928_CLKSEL_CON(32), 8, 5, 0, DFLAGS, NULL),
	DIV(0, "div_aclk_gpu", "mux_aclk_gpu", RK2928_CLKSEL_CON(34), 0, 5, 0, DFLAGS, NULL),
};

#define GFLAGS CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE
static struct rockchip_gate_clock rk3188_gate_clks[] __initdata = {
	/* CLKGATE_CON_0 */
	GATE(0, "gate_core_peri", "div_core_peri", RK2928_CLKGATE_CON(0), 0, 0, GFLAGS),
	GATE(0, "gate_gpll_cpu", "gpll", RK2928_CLKGATE_CON(0), 1, 0, GFLAGS),
	GATE(0, "gate_ddr", "div_ddr", RK2928_CLKGATE_CON(0), 2, 0, GFLAGS),
	GATE(0, "gate_aclk_cpu", "div_aclk_cpu", RK2928_CLKGATE_CON(0), 3, 0, GFLAGS),
	GATE(0, "gate_hclk_cpu", "div_hclk_cpu", RK2928_CLKGATE_CON(0), 4, 0, GFLAGS),
	GATE(0, "gate_pclk_cpu", "div_pclk_cpu", RK2928_CLKGATE_CON(0), 5, 0, GFLAGS),
	GATE(0, "gate_atclk_cpu", "gate_pclk_cpu", RK2928_CLKGATE_CON(0), 6, 0, GFLAGS),
	GATE(0, "gate_aclk_core", "div_aclk_core", RK2928_CLKGATE_CON(0), 7, 0, GFLAGS),
	/* reserved */
	GATE(0, "gate_div_i2s", "div_i2s", RK2928_CLKGATE_CON(0), 9, 0, GFLAGS),
	GATE(0, "gate_frac_i2s", "frac_i2s", RK2928_CLKGATE_CON(0), 10, 0, GFLAGS),
	/* reserved */
	/* reserved */
	GATE(0, "gate_div_spdif", "div_spdif", RK2928_CLKGATE_CON(13), 13, 0, GFLAGS),
	GATE(0, "gate_frac_spdif", "frac_spdif", RK2928_CLKGATE_CON(0), 14, 0, GFLAGS),
	GATE(0, "gate_testclk", "dummy", RK2928_CLKGATE_CON(0), 15, 0, GFLAGS),

	/* CLKGATE_CON_1 */
	GATE(0, "gate_timer0", "xin24m", RK2928_CLKGATE_CON(1), 0, 0, GFLAGS),
	GATE(0, "gate_timer1", "xin24m", RK2928_CLKGATE_CON(1), 1, 0, GFLAGS),
	GATE(0, "gate_timer3", "xin24m", RK2928_CLKGATE_CON(1), 2, 0, GFLAGS),
	GATE(0, "gate_jtag", "dummy", RK2928_CLKGATE_CON(1), 3, 0, GFLAGS),
	GATE(0, "gate_aclk_lcdc1_src", "div_aclk_lcdc1_pre", RK2928_CLKGATE_CON(1), 4, 0, GFLAGS),
	GATE(0, "gate_otgphy0", "dummy480m", RK2928_CLKGATE_CON(1), 5, 0, GFLAGS),
	GATE(0, "gate_otgphy1", "dummy480m", RK2928_CLKGATE_CON(1), 6, 0, GFLAGS),
	GATE(0, "gate_gpll_ddr", "gpll", RK2928_CLKGATE_CON(1), 7, 0, GFLAGS),
	GATE(0, "gate_div_uart0", "div_uart0", RK2928_CLKGATE_CON(1), 8, 0, GFLAGS),
	GATE(0, "gate_frac_uart0", "frac_uart0", RK2928_CLKGATE_CON(1), 9, 0, GFLAGS),
	GATE(0, "gate_div_uart1", "div_uart1", RK2928_CLKGATE_CON(1), 10, 0, GFLAGS),
	GATE(0, "gate_frac_uart1", "frac_uart1", RK2928_CLKGATE_CON(1), 11, 0, GFLAGS),
	GATE(0, "gate_div_uart2", "div_uart2", RK2928_CLKGATE_CON(1), 12, 0, GFLAGS),
	GATE(0, "gate_frac_uart2", "frac_uart2", RK2928_CLKGATE_CON(1), 13, 0, GFLAGS),
	GATE(0, "gate_div_uart3", "div_uart3", RK2928_CLKGATE_CON(1), 14, 0, GFLAGS),
	GATE(0, "gate_frac_uart3", "frac_uart3", RK2928_CLKGATE_CON(1), 15, 0, GFLAGS),

	/* CLKGATE_CON_2 */
	GATE(0, "gate_peri_src", "gate_aclk_peri", RK2928_CLKGATE_CON(2), 0, 0, GFLAGS),
	GATE(0, "gate_aclk_peri", "div_aclk_peri", RK2928_CLKGATE_CON(2), 1, 0, GFLAGS),
	GATE(0, "gate_hclk_peri", "div_hclk_peri", RK2928_CLKGATE_CON(2), 2, 0, GFLAGS),
	GATE(0, "gate_pclk_peri", "div_pclk_peri", RK2928_CLKGATE_CON(2), 3, 0, GFLAGS),
	GATE(0, "gate_smc", "gate_hclk_peri", RK2928_CLKGATE_CON(2), 4, 0, GFLAGS),
	GATE(0, "gate_div_mac", "div_mac", RK2928_CLKGATE_CON(2), 5, 0, GFLAGS),
	GATE(0, "gate_div_hsadc", "div_hsadc", RK2928_CLKGATE_CON(2), 6, 0, GFLAGS),
	GATE(0, "gate_frac_hsadc", "frac_hsadc", RK2928_CLKGATE_CON(2), 7, 0, GFLAGS),
	GATE(SCLK_SARADC, "gate_div_saradc", "div_saradc", RK2928_CLKGATE_CON(2), 8, 0, GFLAGS),
	GATE(0, "gate_div_spi0", "div_spi0", RK2928_CLKGATE_CON(2), 9, 0, GFLAGS),
	GATE(0, "gate_div_spi1", "div_spi1", RK2928_CLKGATE_CON(2), 10, 0, GFLAGS),
	GATE(SCLK_MMC0, "gate_div_mmc0", "div_mmc0", RK2928_CLKGATE_CON(2), 11, 0, GFLAGS),
	GATE(0, "gate_mac_lbtest", "dummy", RK2928_CLKGATE_CON(2), 12, 0, GFLAGS),
	GATE(SCLK_MMC1, "gate_div_mmc1", "div_mmc1", RK2928_CLKGATE_CON(2), 13, 0, GFLAGS),
	GATE(SCLK_MMC2, "gate_div_mmc2", "div_mmc2", RK2928_CLKGATE_CON(2), 14, 0, GFLAGS),
	/* reserved */

	/* CLKGATE_CON_3 */
	GATE(0, "gate_aclk_lcdc0_src", "div_aclk_lcdc0_pre", RK2928_CLKGATE_CON(3), 0, 0, GFLAGS),
	GATE(0, "gate_dclk_lcdc0_src", "div_dclk_lcdc0", RK2928_CLKGATE_CON(3), 1, 0, GFLAGS),
	GATE(0, "gate_dclk_lcdc1_src", "div_dclk_lcdc1", RK2928_CLKGATE_CON(3), 2, 0, GFLAGS),
	GATE(0, "gate_pclkin_cif", "dummy", RK2928_CLKGATE_CON(3), 3, 0, GFLAGS),
	GATE(0, "gate_timer2", "xin24m", RK2928_CLKGATE_CON(3), 4, 0, GFLAGS),
	GATE(0, "gate_timer4", "xin24m", RK2928_CLKGATE_CON(3), 5, 0, GFLAGS),
	GATE(0, "gate_hsicphy", "dummy", RK2928_CLKGATE_CON(3), 6, 0, GFLAGS),
	GATE(0, "gate_div_cif_out", "div_cif_out", RK2928_CLKGATE_CON(3), 7, 0, GFLAGS),
	GATE(0, "gate_timer5", "xin24m", RK2928_CLKGATE_CON(3), 8, 0, GFLAGS),
	GATE(0, "gate_div_aclk_vepu", "div_aclk_vepu", RK2928_CLKGATE_CON(3), 9, 0, GFLAGS),
	GATE(0, "gate_hclk_vepu", "dummy", RK2928_CLKGATE_CON(3), 10, 0, GFLAGS),
	GATE(0, "gate_div_aclk_vdpu", "div_aclk_vdpu", RK2928_CLKGATE_CON(3), 11, 0, GFLAGS),
	GATE(0, "gate_hclk_vdpu", "dummy", RK2928_CLKGATE_CON(3), 12, 0, GFLAGS),
	/* reserved 13 */
	GATE(0, "gate_timer6", "xin24m", RK2928_CLKGATE_CON(3), 14, 0, GFLAGS),
	GATE(0, "gate_aclk_gpu_src", "dummy", RK2928_CLKGATE_CON(3), 15, 0, GFLAGS),

	/* CLKGATE_CON_4 */
	GATE(0, "gate_hclk_peri_axi_matrix", "gate_hclk_peri", RK2928_CLKGATE_CON(4), 0, 0, GFLAGS),
	GATE(0, "gate_pclk_peri_axi_matrix", "gate_pclk_peri", RK2928_CLKGATE_CON(4), 1, 0, GFLAGS),
	GATE(0, "gate_aclk_cpu_peri", "gate_aclk_peri", RK2928_CLKGATE_CON(4), 2, 0, GFLAGS),
	GATE(0, "gate_aclk_peri_axi_matrix", "gate_aclk_peri", RK2928_CLKGATE_CON(4), 3, 0, GFLAGS),
	GATE(0, "gate_aclk_peri_niu", "gate_aclk_peri", RK2928_CLKGATE_CON(4), 4, 0, GFLAGS),
	GATE(0, "gate_hclk_peri_usb", "gate_hclk_peri", RK2928_CLKGATE_CON(4), 5, 0, GFLAGS),
	GATE(0, "gate_hclk_peri_ahb_arbi", "gate_hclk_peri", RK2928_CLKGATE_CON(4), 6, 0, GFLAGS),
	GATE(0, "gate_hclk_peri_emem", "gate_hclk_peri", RK2928_CLKGATE_CON(4), 7, 0, GFLAGS),
	GATE(0, "gate_hclk_cpubus", "gate_hclk_cpu", RK2928_CLKGATE_CON(4), 8, 0, GFLAGS),
	GATE(0, "gate_hclk_ahb2apb", "gate_hclk_cpu", RK2928_CLKGATE_CON(4), 9, 0, GFLAGS),
	GATE(0, "gate_aclk_strc_sys", "gate_aclk_cpu", RK2928_CLKGATE_CON(4), 10, 0, GFLAGS),
	/* reserved 11 */
	GATE(0, "gate_aclk_intmem", "gate_aclk_cpu", RK2928_CLKGATE_CON(4), 12, 0, GFLAGS),
	/* reserved 13 */
	GATE(0, "gate_hclk_imem0", "gate_hclk_cpu", RK2928_CLKGATE_CON(4), 14, 0, GFLAGS),
	GATE(0, "gate_hclk_imem1", "gate_hclk_cpu", RK2928_CLKGATE_CON(4), 15, 0, GFLAGS),

	/* CLKGATE_CON_5 */
	GATE(ACLK_DMAC0, "gate_aclk_dmac0", "gate_aclk_cpu", RK2928_CLKGATE_CON(5), 0, 0, GFLAGS),
	GATE(ACLK_DMAC1, "gate_aclk_dmac1", "gate_aclk_peri", RK2928_CLKGATE_CON(5), 1, 0, GFLAGS),
	GATE(0, "gate_pclk_efuse", "gate_pclk_cpu", RK2928_CLKGATE_CON(5), 2, 0, GFLAGS),
	GATE(0, "gate_pclk_tzpc", "gate_pclk_cpu", RK2928_CLKGATE_CON(5), 3, 0, GFLAGS),
	GATE(PCLK_GRF, "gate_pclk_grf", "gate_pclk_cpu", RK2928_CLKGATE_CON(5), 4, 0, GFLAGS),
	GATE(PCLK_PMU, "gate_pclk_pmu", "gate_pclk_cpu", RK2928_CLKGATE_CON(5), 5, 0, GFLAGS),
	GATE(0, "gate_hclk_rom", "gate_hclk_cpu", RK2928_CLKGATE_CON(5), 6, 0, GFLAGS),
	GATE(0, "gate_pclk_ddrupctl", "gate_pclk_cpu", RK2928_CLKGATE_CON(5), 7, 0, GFLAGS),
	GATE(0, "gate_aclk_smc", "gate_aclk_peri", RK2928_CLKGATE_CON(5), 8, 0, GFLAGS),
	GATE(0, "gate_hclk_nand", "gate_hclk_peri", RK2928_CLKGATE_CON(5), 9, 0, GFLAGS),
	GATE(HCLK_MMC0, "gate_hclk_mmc0", "gate_hclk_peri", RK2928_CLKGATE_CON(5), 10, 0, GFLAGS),
	GATE(HCLK_MMC1, "gate_hclk_mmc1", "gate_hclk_peri", RK2928_CLKGATE_CON(5), 11, 0, GFLAGS),
	GATE(HCLK_MMC2, "gate_hclk_mmc2", "gate_hclk_peri", RK2928_CLKGATE_CON(5), 12, 0, GFLAGS),
	GATE(HCLK_OTG0, "gate_hclk_otg0", "gate_hclk_peri_usb", RK2928_CLKGATE_CON(5), 13, 0, GFLAGS),
	/* reserved 14:15 */

	/* CLKGATE_CON_6 */
	GATE(0, "gate_aclk_lcdc0", "gate_aclk_vio0", RK2928_CLKGATE_CON(6), 0, 0, GFLAGS),
	GATE(0, "gate_hclk_lcdc0", "gate_hclk_cpu", RK2928_CLKGATE_CON(6), 1, 0, GFLAGS),
	GATE(0, "gate_hclk_lcdc1", "gate_aclk_cpu", RK2928_CLKGATE_CON(6), 2, 0, GFLAGS),
	GATE(0, "gate_aclk_lcdc1", "gate_aclk_vio1", RK2928_CLKGATE_CON(6), 3, 0, GFLAGS),
	GATE(0, "gate_hclk_cif", "gate_hclk_cpu", RK2928_CLKGATE_CON(6), 4, 0, GFLAGS),
	GATE(0, "gate_aclk_cif", "gate_aclk_vio0", RK2928_CLKGATE_CON(6), 5, 0, GFLAGS),
	/* reserved 6:7 */
	GATE(0, "gate_aclk_ipp", "gate_aclk_vio0", RK2928_CLKGATE_CON(6), 8, 0, GFLAGS),
	GATE(0, "gate_hclk_ipp", "gate_hclk_cpu", RK2928_CLKGATE_CON(6), 9, 0, GFLAGS),
	GATE(0, "gate_hclk_rga", "gate_hclk_cpu", RK2928_CLKGATE_CON(6), 10, 0, GFLAGS),
	GATE(0, "gate_aclk_rga", "gate_aclk_vio1", RK2928_CLKGATE_CON(6), 11, 0, GFLAGS),
	GATE(0, "gate_hclk_vio_bus", "gate_hclk_cpu", RK2928_CLKGATE_CON(6), 12, 0, GFLAGS),
	GATE(0, "gate_aclk_vio0", "gate_div_aclk_lcdc0", RK2928_CLKGATE_CON(6), 13, 0, GFLAGS),
	/* reserved 14:15 */

	/* CLKGATE_CON_7 */
	GATE(HCLK_EMAC, "gate_hclk_emac", "gate_hclk_peri", RK2928_CLKGATE_CON(7), 0, 0, GFLAGS),
	GATE(HCLK_SPDIF, "gate_hclk_spdif", "gate_hclk_cpu", RK2928_CLKGATE_CON(7), 1, 0, GFLAGS),
	GATE(HCLK_I2S, "gate_hclk_i2s", "gate_hclk_cpu", RK2928_CLKGATE_CON(7), 2, 0, GFLAGS),
	GATE(HCLK_OTG1, "gate_hclk_otg1", "gate_hclk_peri_usb", RK2928_CLKGATE_CON(7), 3, 0, GFLAGS),
	GATE(HCLK_HSIC, "gate_hclk_hsic", "gate_hclk_peri", RK2928_CLKGATE_CON(7), 4, 0, GFLAGS),
	GATE(HCLK_HSADC, "gate_hclk_hsadc", "gate_hclk_peri", RK2928_CLKGATE_CON(7), 5, 0, GFLAGS),
	GATE(HCLK_PIDF, "gate_hclk_pidf", "gate_hclk_peri", RK2928_CLKGATE_CON(7), 6, 0, GFLAGS),
	GATE(PCLK_TIMER0, "gate_pclk_timer0", "gate_pclk_cpu", RK2928_CLKGATE_CON(7), 7, 0, GFLAGS),
	/* reserved 8 */
	GATE(PCLK_TIMER2, "gate_pclk_timer2", "gate_pclk_cpu", RK2928_CLKGATE_CON(7), 9, 0, GFLAGS),
	GATE(PCLK_PWM01, "gate_pclk_pwm01", "gate_pclk_cpu", RK2928_CLKGATE_CON(7), 10, 0, GFLAGS),
	GATE(PCLK_PWM23, "gate_pclk_pwm23", "gate_pclk_peri", RK2928_CLKGATE_CON(7), 11, 0, GFLAGS),
	GATE(PCLK_SPI0, "gate_pclk_spi0", "gate_pclk_peri", RK2928_CLKGATE_CON(7), 12, 0, GFLAGS),
	GATE(PCLK_SPI1, "gate_pclk_spi1", "gate_pclk_peri", RK2928_CLKGATE_CON(7), 13, 0, GFLAGS),
	GATE(PCLK_SARADC, "gate_pclk_saradc", "gate_pclk_peri", RK2928_CLKGATE_CON(7), 14, 0, GFLAGS),
	GATE(PCLK_WDT, "gate_pclk_wdt", "gate_pclk_peri", RK2928_CLKGATE_CON(7), 15, 0, GFLAGS),

	/* CLKGATE_CON_8 */
	GATE(PCLK_UART0, "gate_pclk_uart0", "div_hclk_ahb2apb", RK2928_CLKGATE_CON(8), 0, 0, GFLAGS),
	GATE(PCLK_UART1, "gate_pclk_uart1", "div_hclk_ahb2apb", RK2928_CLKGATE_CON(8), 1, 0, GFLAGS),
	GATE(PCLK_UART2, "gate_pclk_uart2", "gate_pclk_peri", RK2928_CLKGATE_CON(8), 2, 0, GFLAGS),
	GATE(PCLK_UART3, "gate_pclk_uart3", "gate_pclk_peri", RK2928_CLKGATE_CON(8), 3, 0, GFLAGS),
	GATE(PCLK_I2C0, "gate_pclk_i2c0", "gate_pclk_cpu", RK2928_CLKGATE_CON(8), 4, 0, GFLAGS),
	GATE(PCLK_I2C1, "gate_pclk_i2c1", "gate_pclk_cpu", RK2928_CLKGATE_CON(8), 5, 0, GFLAGS),
	GATE(PCLK_I2C2, "gate_pclk_i2c2", "gate_pclk_peri", RK2928_CLKGATE_CON(8), 6, 0, GFLAGS),
	GATE(PCLK_I2C3, "gate_pclk_i2c3", "gate_pclk_peri", RK2928_CLKGATE_CON(8), 7, 0, GFLAGS),
	GATE(PCLK_I2C4, "gate_pclk_i2c4", "gate_pclk_peri", RK2928_CLKGATE_CON(8), 8, 0, GFLAGS),
	GATE(PCLK_GPIO0, "gate_pclk_gpio0", "gate_pclk_cpu", RK2928_CLKGATE_CON(8), 9, 0, GFLAGS),
	GATE(PCLK_GPIO1, "gate_pclk_gpio1", "gate_pclk_cpu", RK2928_CLKGATE_CON(8), 10, 0, GFLAGS),
	GATE(PCLK_GPIO2, "gate_pclk_gpio2", "gate_pclk_cpu", RK2928_CLKGATE_CON(8), 11, 0, GFLAGS),
	GATE(PCLK_GPIO3, "gate_pclk_gpio3", "gate_pclk_peri", RK2928_CLKGATE_CON(8), 12, 0, GFLAGS),
	GATE(ACLK_GPS, "gate_aclk_gps", "gate_aclk_peri", RK2928_CLKGATE_CON(8), 13, 0, GFLAGS),
	/* reserved 14:15 */

	/* CLKGATE_CON_9 */
	GATE(0, "gate_core_dbg", "armclk", RK2928_CLKGATE_CON(9), 0, 0, GFLAGS),
	GATE(0, "gate_pclk_dbg", "gate_pclk_cpu", RK2928_CLKGATE_CON(9), 1, 0, GFLAGS),
	GATE(0, "gate_clk_trace", "dummy", RK2928_CLKGATE_CON(9), 2, 0, GFLAGS),
	GATE(0, "gate_atclk", "dummy", RK2928_CLKGATE_CON(9), 3, 0, GFLAGS),
	GATE(CORE_L2C, "gate_core_l2c", "armclk", RK2928_CLKGATE_CON(9), 4, 0, GFLAGS),
	GATE(0, "gate_aclk_vio1", "div_aclk_lcdc1", RK2928_CLKGATE_CON(9), 5, 0, GFLAGS),
	GATE(0, "gate_pclk_publ", "gate_pclk_cpu", RK2928_CLKGATE_CON(9), 6, 0, GFLAGS),
	GATE(0, "gate_aclk_gpu", "div_aclk_gpu", RK2928_CLKGATE_CON(9), 7, 0, GFLAGS),
	/* reserved 8:15 */
};


struct rockchip_clk_init_table rk3188_clk_init_tbl[] __initdata = {
	{ "gpll", NULL, 891000000, 0 },

	{ "mux_aclk_cpu", "gpll", 0, 0 },
	{ "div_aclk_cpu", NULL, 300000000, 0 },
	{ "div_hclk_cpu", NULL, 150000000, 0 },
	{ "div_pclk_cpu", NULL,  75000000, 0 },
	{ "div_hclk_ahb2apb", NULL, 75000000, 0 },

	{ "mux_aclk_peri", "gpll", 0, 0 },
	{ "div_aclk_peri", NULL, 150000000, 0 },
	{ "div_hclk_peri", NULL, 150000000, 0 },
	{ "div_pclk_peri", NULL,  75000000, 0 },

	{ "div_mmc0", NULL,  75000000, 0 },

	{ "gate_div_mac", NULL,  50000000, 0 },

	/* FIXME: is this needed? */
	{ "gate_mac_lbtest", NULL, 0, 1 },
};

static void __init rk3188_clock_apply_init_table(void)
{
	rockchip_clk_init_from_table(rk3188_clk_init_tbl, ARRAY_SIZE(rk3188_clk_init_tbl));
}

static void __init rk3188_clk_init(struct device_node *np)
{
	void __iomem *reg_base, *reg_grf_soc_status;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return;
	}

	reg_grf_soc_status = of_iomap(np, 1);
	if (!reg_grf_soc_status) {
		pr_err("%s: could not map soc_status register\n", __func__);
		return;
	}

	rockchip_clk_init(np, reg_base, NR_CLKS);

	rockchip_clk_register_plls(rk3188_pll_clks,
				   ARRAY_SIZE(rk3188_pll_clks),
				   reg_grf_soc_status);

	rockchip_clk_register_mux(rk3188_mux_clks,
				  ARRAY_SIZE(rk3188_mux_clks));
	rockchip_clk_register_div(rk3188_div_clks,
				  ARRAY_SIZE(rk3188_div_clks));
	rockchip_clk_register_gate(rk3188_gate_clks,
				   ARRAY_SIZE(rk3188_gate_clks));

	rockchip_clk_register_armclk(SCLK_ARMCLK, "armclk", mux_armclk_p,
				     ARRAY_SIZE(mux_armclk_p), reg_base, np);

	rockchip_register_softrst(np, 9, reg_base + RK2928_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_clk_apply_init_table = rk3188_clock_apply_init_table;
}
CLK_OF_DECLARE(rk3188_cru, "rockchip,rk3188-cru", rk3188_clk_init);
