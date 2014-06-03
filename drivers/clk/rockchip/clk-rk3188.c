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

#define RK3188_GRF_SOC_CON1	0xa4
#define RK3188_GRF_SOC_STATUS	0xac

enum rk3188_plls {
	apll, cpll, dpll, gpll,
};

struct rockchip_pll_rate_table rk3188_apll_rates[] = {
	RK3066_PLL_RATE(2208000000, 1, 92, 1),
	RK3066_PLL_RATE(2184000000, 1, 91, 1),
	RK3066_PLL_RATE(2160000000, 1, 90, 1),
	RK3066_PLL_RATE(2136000000, 1, 89, 1),
	RK3066_PLL_RATE(2112000000, 1, 88, 1),
	RK3066_PLL_RATE(2088000000, 1, 87, 1),
	RK3066_PLL_RATE(2064000000, 1, 86, 1),
	RK3066_PLL_RATE(2040000000, 1, 85, 1),
	RK3066_PLL_RATE(2016000000, 1, 84, 1),
	RK3066_PLL_RATE(1992000000, 1, 83, 1),
	RK3066_PLL_RATE(1968000000, 1, 82, 1),
	RK3066_PLL_RATE(1944000000, 1, 81, 1),
	RK3066_PLL_RATE(1920000000, 1, 80, 1),
	RK3066_PLL_RATE(1896000000, 1, 79, 1),
	RK3066_PLL_RATE(1872000000, 1, 78, 1),
	RK3066_PLL_RATE(1848000000, 1, 77, 1),
	RK3066_PLL_RATE(1824000000, 1, 76, 1),
	RK3066_PLL_RATE(1800000000, 1, 75, 1),
	RK3066_PLL_RATE(1776000000, 1, 74, 1),
	RK3066_PLL_RATE(1752000000, 1, 73, 1),
	RK3066_PLL_RATE(1728000000, 1, 72, 1),
	RK3066_PLL_RATE(1704000000, 1, 71, 1),
	RK3066_PLL_RATE(1680000000, 1, 70, 1),
	RK3066_PLL_RATE(1656000000, 1, 69, 1),
	RK3066_PLL_RATE(1632000000, 1, 68, 1),
	RK3066_PLL_RATE(1608000000, 1, 67, 1),
	RK3066_PLL_RATE(1560000000, 1, 65, 1),
	RK3066_PLL_RATE(1512000000, 1, 63, 1),
	RK3066_PLL_RATE(1488000000, 1, 62, 1),
	RK3066_PLL_RATE(1464000000, 1, 61, 1),
	RK3066_PLL_RATE(1440000000, 1, 60, 1),
	RK3066_PLL_RATE(1416000000, 1, 59, 1),
	RK3066_PLL_RATE(1392000000, 1, 58, 1),
	RK3066_PLL_RATE(1368000000, 1, 57, 1),
	RK3066_PLL_RATE(1344000000, 1, 56, 1),
	RK3066_PLL_RATE(1320000000, 1, 55, 1),
	RK3066_PLL_RATE(1296000000, 1, 54, 1),
	RK3066_PLL_RATE(1272000000, 1, 53, 1),
	RK3066_PLL_RATE(1248000000, 1, 52, 1),
	RK3066_PLL_RATE(1224000000, 1, 51, 1),
	RK3066_PLL_RATE(1200000000, 1, 50, 1),
	RK3066_PLL_RATE(1176000000, 1, 49, 1),
	RK3066_PLL_RATE(1128000000, 1, 47, 1),
	RK3066_PLL_RATE(1104000000, 1, 46, 1),
	RK3066_PLL_RATE(1008000000, 1, 84, 2),
	RK3066_PLL_RATE( 912000000, 1, 76, 2),
	RK3066_PLL_RATE( 888000000, 1, 74, 2),
	RK3066_PLL_RATE( 816000000, 1, 68, 2),
	RK3066_PLL_RATE( 792000000, 1, 66, 2),
	RK3066_PLL_RATE( 696000000, 1, 58, 2),
	RK3066_PLL_RATE( 600000000, 1, 50, 2),
	RK3066_PLL_RATE( 504000000, 1, 84, 4),
	RK3066_PLL_RATE( 408000000, 1, 68, 4),
	RK3066_PLL_RATE( 312000000, 1, 52, 4),
	RK3066_PLL_RATE( 252000000, 1, 84, 8),
	RK3066_PLL_RATE( 216000000, 1, 72, 8),
	RK3066_PLL_RATE( 126000000, 1, 84, 16),
	RK3066_PLL_RATE(  48000000, 1, 64, 32),
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

PNAME(mux_pll_p)		= { "xin24m", "xin32k" };
PNAME(mux_armclk_p)		= { "apll", "gpll_armclk" };
PNAME(mux_ddrphy_p)		= { "dpll", "gpll_ddr" };
PNAME(mux_pll_src_gpll_cpll_p)	= { "gpll", "cpll" };
PNAME(mux_pll_src_cpll_gpll_p)	= { "cpll", "gpll" };
PNAME(mux_aclk_cpu_p)		= { "apll", "gpll" };
PNAME(mux_sclk_cif_p)		= { "cif_pre", "xin24m" };
PNAME(mux_sclk_i2s_p)		= { "i2s_pre", "i2s_frac", "xin12m" };
PNAME(mux_sclk_spdif_p)		= { "spdif_pre", "spdif_frac", "xin12m" };
PNAME(mux_sclk_uart0_p)		= { "uart0_pll", "uart0_frac", "xin24m" };
PNAME(mux_sclk_uart1_p)		= { "uart1_pll", "uart1_frac", "xin24m" };
PNAME(mux_sclk_uart2_p)		= { "uart2_pll", "uart2_frac", "xin24m" };
PNAME(mux_sclk_uart3_p)		= { "uart3_pll", "uart3_frac", "xin24m" };
PNAME(mux_sclk_hsadc_p)		= { "hsadc_pll", "hsadc_frac", "ext_hsadc" };
PNAME(mux_mac_p)		= { "gpll", "dpll" };
PNAME(mux_sclk_macref_p)	= { "mac_src", "ext_rmii" };
PNAME(mux_hsicphy_p)		= { "sclk_otgphy0", "sclk_otgphy1",
				    "gpll", "cpll" };
PNAME(mux_testclk_p)		= { "pclk_cpu", "hclk_peri", "dclk_lcdc0_src",
				    "sclk_uart0", "sclk_ddr", "hclk_vpu" };
PNAME(mux_aclk_vpu_p)		= { "aclk_vepu", "aclk_vdpu" };

static struct rockchip_pll_clock rk3188_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3066, 0, "apll", mux_pll_p, 0, RK2928_PLL_CON(0),
		     RK2928_MODE_CON, 0, 6, rk3188_apll_rates),
	[dpll] = PLL(pll_rk3066, 0, "dpll", mux_pll_p, 0, RK2928_PLL_CON(4),
		     RK2928_MODE_CON, 4, 5, NULL),
	[cpll] = PLL(pll_rk3066, 0, "cpll", mux_pll_p, 0, RK2928_PLL_CON(8),
		     RK2928_MODE_CON, 8, 7, rk3188_cpll_rates),
	[gpll] = PLL(pll_rk3066, 0, "gpll", mux_pll_p, 0, RK2928_PLL_CON(12),
		     RK2928_MODE_CON, 12, 8, rk3188_gpll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

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

static struct rockchip_clk_branch rk3188_clk_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 2
	 */

	GATE(0, "gpll_armclk", "gpll", 0, RK2928_CLKGATE_CON(0), 1, GFLAGS),
//	COMPOSITE_NOGATE(ARMCLK, "armclk", mux_armclk_p, 0,
//			RK2928_CLKSEL_CON(0), 8, 1, MFLAGS, 9, 5, DFLAGS),

	/* these two are set by the cpuclk and should not be changed */
	COMPOSITE_NOMUX_DIVTBL(CORE_PERI, "core_peri", "armclk", 0,
			RK2928_CLKSEL_CON(0), 6, 2, DFLAGS | CLK_DIVIDER_READ_ONLY,
			div_core_peri_t, RK2928_CLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NOMUX_DIVTBL(0, "aclk_core", "armclk", 0,
			RK2928_CLKSEL_CON(1), 3, 3, DFLAGS | CLK_DIVIDER_READ_ONLY,
			div_aclk_core_t, RK2928_CLKGATE_CON(0), 7, GFLAGS),

	COMPOSITE(0, "aclk_vepu", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(32), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 9, GFLAGS),
	GATE(0, "hclk_vepu", "aclk_vepu", 0,
			RK2928_CLKGATE_CON(3), 10, GFLAGS),
	COMPOSITE(0, "aclk_vdpu", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(32), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 11, GFLAGS),
	GATE(0, "hclk_vdpu", "aclk_vdpu", 0,
			RK2928_CLKGATE_CON(3), 12, GFLAGS),
	MUXGRF(0, "aclk_vpu", mux_aclk_vpu_p, 0,
			RK3188_GRF_SOC_CON1, 10, 1, MFLAGS),

	GATE(0, "gpll_ddr", "gpll", 0,
			RK2928_CLKGATE_CON(1), 7, GFLAGS),
	COMPOSITE(0, "ddrphy", mux_ddrphy_p, 0,
			RK2928_CLKSEL_CON(26), 8, 1, MFLAGS, 0, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK2928_CLKGATE_CON(0), 2, GFLAGS),

	/* do not source aclk_cpu_pre from the apll, to keep complexity down */
	COMPOSITE_NOGATE(0, "aclk_cpu_pre", mux_aclk_cpu_p, CLK_SET_RATE_NO_REPARENT,
			RK2928_CLKSEL_CON(0), 5, 1, MFLAGS, 0, 5, DFLAGS),
	GATE(0, "aclk_cpu", "aclk_cpu_pre", 0,
			RK2928_CLKGATE_CON(0), 3, GFLAGS),

	DIV(0, "pclk_cpu_pre", "aclk_cpu_pre", 0,
			RK2928_CLKGATE_CON(1), 12, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO),
	GATE(0, "atclk_cpu", "pclk_cpu_pre", 0,
			RK2928_CLKGATE_CON(0), 6, GFLAGS),
	GATE(0, "pclk_cpu", "pclk_cpu_pre", 0,
			RK2928_CLKGATE_CON(0), 5, GFLAGS),
	DIV(0, "hclk_cpu_pre", "aclk_cpu_pre", 0,
			RK2928_CLKGATE_CON(1), 8, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO),
	COMPOSITE_NOMUX(0, "hclk_ahb2apb", "hclk_cpu_pre", 0,
			RK2928_CLKSEL_CON(1), 14, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK2928_CLKGATE_CON(4), 9, GFLAGS),
	GATE(0, "hclk_cpu", "hclk_cpu_pre", 0,
			RK2928_CLKGATE_CON(0), 4, GFLAGS),

	COMPOSITE(0, "aclk_gpu_src", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(34), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 15, GFLAGS),
	GATE(0, "aclk_gpu", "aclk_gpu_src", 0,
			RK2928_CLKGATE_CON(9), 7, GFLAGS),

	COMPOSITE(0, "aclk_lcdc0_pre", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(31), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 0, GFLAGS),
	COMPOSITE(0, "aclk_lcdc1_pre", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(31), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(1), 4, GFLAGS),

	COMPOSITE(0, "aclk_peri_pre", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(10), 15, 1, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(2), 0, GFLAGS),
	GATE(0, "aclk_peri", "aclk_peri_pre", 0,
			RK2928_CLKGATE_CON(2), 1, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_peri", "aclk_peri_pre", 0,
			RK2928_CLKSEL_CON(10), 8, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK2928_CLKGATE_CON(2), 2, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_peri", "aclk_peri_pre", 0,
			RK2928_CLKSEL_CON(10), 12, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK2928_CLKGATE_CON(2), 3, GFLAGS),

	/*
	 * Clock-Architecture Diagram 3
	 */

	COMPOSITE(0, "dclk_lcdc0_src", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(27), 0, 1, MFLAGS, 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(3), 1, GFLAGS),
	COMPOSITE(0, "dclk_lcdc1_src", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(28), 0, 1, MFLAGS, 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(3), 2, GFLAGS),

	COMPOSITE(0, "cif_pre", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(29), 0, 1, MFLAGS, 1, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 7, GFLAGS),
	MUX(0, "sclk_cif", mux_sclk_cif_p, 0,
			RK2928_CLKSEL_CON(29), 7, 1, MFLAGS),
	//FIXME: CLKSEL_CON30[8]: add inverter

	GATE(0, "pclk_cif_in", "ext_cif", 0,
			RK2928_CLKGATE_CON(3), 3, GFLAGS),

	MUX(0, "i2s_src", mux_pll_src_gpll_cpll_p, 0,
			RK2928_CLKSEL_CON(2), 15, 1, MFLAGS),
	COMPOSITE_NOMUX(0, "i2s_pre", "i2s_src", 0,
			RK2928_CLKSEL_CON(3), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(0), 9, GFLAGS),
	COMPOSITE_FRAC(0, "i2s_frac", "i2s_pre", 0, RK2928_CLKGATE_CON(7), 0,
			RK2928_CLKGATE_CON(0), 10, GFLAGS),
	MUX(0, "sclk_i2s", mux_sclk_i2s_p, 0,
			RK2928_CLKSEL_CON(3), 8, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "spdif_pre", "i2s_src", 0,
			RK2928_CLKSEL_CON(5), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(13), 13, GFLAGS),
	COMPOSITE_FRAC(0, "spdif_frac", "spdif_pll", 0,
			RK2928_CLKGATE_CON(9), 0,
			RK2928_CLKGATE_CON(0), 14, GFLAGS),
	MUX(0, "sclk_spdif", mux_sclk_spdif_p, 0,
			RK2928_CLKSEL_CON(5), 8, 2, MFLAGS),

	/*
	 * the 480m are generated inside the usb block from these clocks,
	 * but they are also a source for the hsicphy clock.
	 */
	GATE(0, "sclk_otgphy0", "usb480m", 0,
			RK2928_CLKGATE_CON(1), 5, GFLAGS),
	GATE(0, "sclk_otgphy1", "usb480m", 0,
			RK2928_CLKGATE_CON(1), 6, GFLAGS),

	COMPOSITE_NODIV(0, "sclk_hsicphy_480m", mux_hsicphy_p, 0,
			RK2928_CLKSEL_CON(30), 0, 2, DFLAGS,
			RK2928_CLKGATE_CON(3), 6, GFLAGS),
	DIV(0, "sclk_hsicphy_12m", "sclk_hsicphy_480m", 0,
			RK2928_CLKGATE_CON(11), 8, 6, DFLAGS),

	COMPOSITE(0, "mac_src", mux_mac_p, 0,
			RK2928_CLKSEL_CON(21), 0, 1, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(2), 5, GFLAGS),
	MUX(SCLK_MAC, "sclk_mac_ref", mux_sclk_macref_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(21), 4, 1, MFLAGS),

	GATE(0, "sclk_mac_lbtest", "sclk_mac_ref",
			RK2928_CLKGATE_CON(2), 12, 0, GFLAGS),

	COMPOSITE(0, "hsadc_pll", mux_pll_src_gpll_cpll_p, 0,
			RK2928_CLKSEL_CON(22), 0, 1, MFLAGS, 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(2), 6, GFLAGS),
	COMPOSITE_FRAC(0, "hsadc_frac", "hsadc_pll",
			RK2928_CLKGATE_CON(23), 0,
			RK2928_CLKGATE_CON(2), 7, 0, GFLAGS),
	MUX(0, "sclk_hsadc", mux_sclk_hsadc_p, 0,
			RK2928_CLKSEL_CON(22), 4, 2, MFLAGS),
	// FIXME: add hsadc_inverter at CLKSEL22[7]

	COMPOSITE_NOMUX(SCLK_SARADC, "sclk_saradc", "xin24m", 0,
			RK2928_CLKSEL_CON(24), 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(2), 8, GFLAGS),

	/*
	 * Clock-Architecture Diagram 4
	 */

	GATE(0, "sclk_smc", "hclk_peri",
			RK2928_CLKGATE_CON(2), 4, 0, GFLAGS),

	COMPOSITE_NOMUX(SCLK_SPI0, "sclk_spi0", "pclk_peri", 0,
			RK2928_CLKSEL_CON(25), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(2), 9, GFLAGS),
	COMPOSITE_NOMUX(SCLK_SPI1, "sclk_spi1", "pclk_peri", 0,
			RK2928_CLKSEL_CON(25), 8, 7, DFLAGS,
			RK2928_CLKGATE_CON(2), 10, GFLAGS),

	COMPOSITE_NOMUX(SCLK_MMC0, "sclk_mmc0", "hclk_peri", 0,
			RK2928_CLKSEL_CON(11), 0, 6, DFLAGS,
			RK2928_CLKGATE_CON(2), 11, GFLAGS),
	COMPOSITE_NOMUX(SCLK_MMC1, "sclk_mmc1", "hclk_peri", 0,
			RK2928_CLKSEL_CON(12), 0, 6, DFLAGS,
			RK2928_CLKGATE_CON(2), 13, GFLAGS),
	COMPOSITE_NOMUX(SCLK_MMC2, "sclk_mmc2", "hclk_peri", 0,
			RK2928_CLKSEL_CON(12), 8, 6, DFLAGS,
			RK2928_CLKGATE_CON(2), 14, GFLAGS),

	MUX(0, "uart_pll", mux_pll_src_gpll_cpll_p, 0,
			RK2928_CLKSEL_CON(12), 15, 1, MFLAGS),
	COMPOSITE_NOMUX(0, "uart0_pll", "uart_pll", 0,
			RK2928_CLKSEL_CON(13), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE_FRAC(0, "uart0_frac", "uart0_pll", 0,
			RK2928_CLKSEL_CON(17), 0,
			RK2928_CLKGATE_CON(1), 9, GFLAGS),
	MUX(SCLK_UART0, "sclk_uart0", mux_sclk_uart0_p, 0,
			RK2928_CLKSEL_CON(13), 8, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "uart1_pll", "uart_pll", 0,
			RK2928_CLKSEL_CON(14), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 10, GFLAGS),
	COMPOSITE_FRAC(0, "uart1_frac", "uart1_pll", 0,
			RK2928_CLKSEL_CON(18), 0,
			RK2928_CLKGATE_CON(1), 11, GFLAGS),
	MUX(SCLK_UART1, "sclk_uart1", mux_sclk_uart1_p, 0,
			RK2928_CLKSEL_CON(14), 8, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "uart2_pll", "uart_pll", 0,
			RK2928_CLKSEL_CON(15), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 12, GFLAGS),
	COMPOSITE_FRAC(0, "uart2_frac", "uart2_pll", 0,
			RK2928_CLKSEL_CON(19), 0,
			RK2928_CLKGATE_CON(1), 13, GFLAGS),
	MUX(SCLK_UART2, "sclk_uart2", mux_sclk_uart2_p, 0,
			RK2928_CLKSEL_CON(15), 8, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "uart3_pll", "uart_pll", 0,
			RK2928_CLKSEL_CON(16), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 14, GFLAGS),
	COMPOSITE_FRAC(0, "uart3_frac", "uart3_pll", 0,
			RK2928_CLKSEL_CON(20), 0,
			RK2928_CLKGATE_CON(1), 15, GFLAGS),
	MUX(SCLK_UART3, "sclk_uart3", mux_sclk_uart3_p, 0,
			RK2928_CLKSEL_CON(16), 8, 2, MFLAGS),

	GATE(0, "timer0", "xin24m", 0, RK2928_CLKGATE_CON(1), 0, GFLAGS),
	GATE(0, "timer1", "xin24m", 0, RK2928_CLKGATE_CON(1), 1, GFLAGS),
	GATE(0, "timer2", "xin24m", 0, RK2928_CLKGATE_CON(3), 4, GFLAGS),
	GATE(0, "timer3", "xin24m", 0, RK2928_CLKGATE_CON(1), 2, GFLAGS),
	GATE(0, "timer4", "xin24m", 0, RK2928_CLKGATE_CON(3), 5, GFLAGS),
	GATE(0, "timer5", "xin24m", 0, RK2928_CLKGATE_CON(3), 8, GFLAGS),
	GATE(0, "timer6", "xin24m", 0, RK2928_CLKGATE_CON(3), 14, GFLAGS),

	GATE(0, "jtag", "ext_jtag", 0, RK2928_CLKGATE_CON(1), 3, GFLAGS),

	/*
	 * Clock-Architecture Diagram 5
	 */

	/* clk_core_pre gates */
	GATE(CORE_L2C, "core_l2c", "armclk", 0, RK2928_CLKGATE_CON(9), 4, GFLAGS),
	GATE(0, "core_dbg", "armclk", 0, RK2928_CLKGATE_CON(9), 0, GFLAGS),

	/* aclk_cpu gates */
	GATE(ACLK_DMAC0, "aclk_dmac0", "aclk_cpu", 0, RK2928_CLKGATE_CON(5), 0, GFLAGS),
	GATE(0, "aclk_intmem", "aclk_cpu", 0, RK2928_CLKGATE_CON(4), 12, GFLAGS),
	GATE(0, "aclk_strc_sys", "aclk_cpu", 0, RK2928_CLKGATE_CON(4), 10, GFLAGS),

	/* hclk_cpu gates */
	GATE(0, "hclk_rom", "hclk_cpu", 0, RK2928_CLKGATE_CON(5), 6, GFLAGS),
	GATE(HCLK_I2S, "hclk_i2s", "hclk_cpu", 0, RK2928_CLKGATE_CON(7), 2, GFLAGS),
	GATE(HCLK_SPDIF, "hclk_spdif", "hclk_cpu", 0, RK2928_CLKGATE_CON(7), 1, GFLAGS),
	GATE(0, "hclk_cpubus", "hclk_cpu", 0, RK2928_CLKGATE_CON(4), 8, GFLAGS),
	/* hclk_ahb2apb is part of a clk branch */
	GATE(0, "hclk_vio_bus", "hclk_cpu", 0, RK2928_CLKGATE_CON(6), 12, GFLAGS),
	GATE(0, "hclk_lcdc0", "hclk_cpu", 0, RK2928_CLKGATE_CON(6), 1, GFLAGS),
	GATE(0, "hclk_lcdc1", "aclk_cpu", 0, RK2928_CLKGATE_CON(6), 2, GFLAGS),
	GATE(0, "hclk_cif", "hclk_cpu", 0, RK2928_CLKGATE_CON(6), 4, GFLAGS),
	GATE(0, "hclk_ipp", "hclk_cpu", 0, RK2928_CLKGATE_CON(6), 9, GFLAGS),
	GATE(0, "hclk_rga", "hclk_cpu", 0, RK2928_CLKGATE_CON(6), 10, GFLAGS),
	GATE(0, "hclk_imem0", "hclk_cpu", 0, RK2928_CLKGATE_CON(4), 14, GFLAGS),
 	GATE(0, "hclk_imem1", "hclk_cpu", 0, RK2928_CLKGATE_CON(4), 15, GFLAGS),

	/* hclk_peri gates */
	GATE(0, "hclk_peri_axi_matrix", "hclk_peri", 0, RK2928_CLKGATE_CON(4), 0, GFLAGS),
	GATE(0, "hclk_peri_ahb_arbi", "hclk_peri", 0, RK2928_CLKGATE_CON(4), 6, GFLAGS),
	GATE(0, "hclk_emem_peri", "hclk_peri", 0, RK2928_CLKGATE_CON(4), 7, GFLAGS),
	GATE(HCLK_EMAC, "hclk_emac", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 0, GFLAGS),
	GATE(0, "hclk_nandc", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 9, GFLAGS),
	GATE(0, "hclk_usb_peri", "hclk_peri", 0, RK2928_CLKGATE_CON(4), 5, GFLAGS),
	GATE(HCLK_OTG0, "hclk_usbotg0", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 13, GFLAGS),
	GATE(HCLK_OTG1, "hclk_usbotg1", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 3, GFLAGS),
	GATE(HCLK_HSIC, "hclk_hsic", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 4, GFLAGS),
	GATE(HCLK_HSADC, "hclk_hsadc", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 5, GFLAGS),
	GATE(HCLK_PIDF, "hclk_pidfilter", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 6, GFLAGS),
	GATE(HCLK_MMC0, "hclk_sdmmc", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 10, GFLAGS),
	GATE(HCLK_MMC1, "hclk_sdio", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 11, GFLAGS),
	GATE(HCLK_MMC2, "hclk_emmc", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 12, GFLAGS),

	/* aclk_lcdc0_pre gates */
	GATE(0, "aclk_vio0", "aclk_lcdc0_pre", 0, RK2928_CLKGATE_CON(6), 13, GFLAGS),
	GATE(0, "aclk_lcdc0", "aclk_vio0", 0, RK2928_CLKGATE_CON(6), 0, GFLAGS),
	GATE(0, "aclk_cif", "aclk_vio0", 0, RK2928_CLKGATE_CON(6), 5, GFLAGS),
	GATE(0, "aclk_ipp", "aclk_vio0", 0, RK2928_CLKGATE_CON(6), 8, GFLAGS),

	/* aclk_lcdc1_pre gates */
	GATE(0, "aclk_vio1", "aclk_lcdc1_pre", 0, RK2928_CLKGATE_CON(9), 5, GFLAGS),
	GATE(0, "aclk_lcdc1", "aclk_vio1", 0, RK2928_CLKGATE_CON(6), 3, GFLAGS),
	GATE(0, "aclk_rga", "aclk_vio1", 0, RK2928_CLKGATE_CON(6), 11, GFLAGS),

	/* atclk_cpu gates */
	GATE(0, "atclk", "atclk_cpu", 0, RK2928_CLKGATE_CON(9), 3, GFLAGS),
	GATE(0, "trace", "atclk_cpu", 0, RK2928_CLKGATE_CON(9), 2, GFLAGS),

	/* hclk_cpu_h2p gates */
	GATE(PCLK_UART0, "pclk_uart0", "hclk_ahb2apb", 0, RK2928_CLKGATE_CON(8), 0, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "hclk_ahb2apb", 0, RK2928_CLKGATE_CON(8), 1, GFLAGS),

	/* pclk_cpu gates */
	GATE(PCLK_PWM01, "pclk_pwm01", "pclk_cpu", 0, RK2928_CLKGATE_CON(7), 10, GFLAGS),
	GATE(PCLK_TIMER0, "pclk_timer0", "pclk_cpu", 0, RK2928_CLKGATE_CON(7), 7, GFLAGS),
	GATE(PCLK_TIMER2, "pclk_timer3", "pclk_cpu", 0, RK2928_CLKGATE_CON(7), 9, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 4, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 5, GFLAGS),
	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 9, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 10, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 11, GFLAGS),
	GATE(0, "pclk_efuse", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 2, GFLAGS),
	GATE(0, "pclk_tzpc", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 3, GFLAGS),
	GATE(0, "pclk_ddrupctl", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 7, GFLAGS),
	GATE(0, "pclk_ddrpubl", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 6, GFLAGS),
	GATE(0, "pclk_dbg", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 1, GFLAGS),
	GATE(PCLK_GRF, "pclk_grf", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 4, GFLAGS),
	GATE(PCLK_PMU, "pclk_pmu", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 5, GFLAGS),

	/* aclk_peri */
	GATE(ACLK_DMAC1, "aclk_dmac1", "aclk_peri", 0, RK2928_CLKGATE_CON(5), 1, GFLAGS),
	GATE(0, "aclk_smc", "aclk_peri", 0, RK2928_CLKGATE_CON(5), 8, GFLAGS),
	GATE(ACLK_GPS, "aclk_gps", "aclk_peri", 0, RK2928_CLKGATE_CON(8), 13, GFLAGS),
	GATE(0, "aclk_peri_niu", "aclk_peri", 0, RK2928_CLKGATE_CON(4), 4, GFLAGS),
	GATE(0, "aclk_cpu_peri", "aclk_peri", 0, RK2928_CLKGATE_CON(4), 2, GFLAGS),
	GATE(0, "aclk_peri_axi_matrix", "aclk_peri", 0, RK2928_CLKGATE_CON(4), 3, GFLAGS),

	/* pclk_peri gates */
	GATE(0, "pclk_peri_axi_matrix", "pclk_peri", 0, RK2928_CLKGATE_CON(4), 1, GFLAGS),
	GATE(PCLK_PWM23, "pclk_pwm23", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 11,GFLAGS),
	GATE(PCLK_WDT, "pclk_wdt", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 15, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 12, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 13, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 2, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 3, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 6, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 7, GFLAGS),
	GATE(PCLK_I2C4, "pclk_i2c4", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 8, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 12, GFLAGS),
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 14, GFLAGS),

	COMPOSITE_NODIV(0, "testclk", mux_testclk_p, 0,
			RK2928_MISC_CON, 8, 3, MFLAGS,
			RK2928_CLKGATE_CON(0), 15, GFLAGS),
};


struct rockchip_clk_init_table rk3188_clk_init_tbl[] __initdata = {
	{ "gpll", NULL, 891000000, 0 },
	{ "cpll", NULL, 600000000, 0 },

	{ "aclk_cpu", NULL, 300000000, 0 },

	{ "hclk_cpu", NULL, 150000000, 0 },
	{ "pclk_cpu", NULL,  75000000, 0 },
	{ "hclk_ahb2apb", NULL, 75000000, 0 },

	{ "aclk_peri_pre", "cpll", 150000000, 0 },

	{ "hclk_peri", NULL, 150000000, 0 },
	{ "pclk_peri", NULL,  75000000, 0 },

	{ "sclk_mmc0", NULL,  75000000, 0 },

	{ "sclk_mac_ref", NULL,  50000000, 0 },

	/* FIXME: is this needed? */
	{ "mac_lbtest", NULL, 0, 1 },
};

static void __init rk3188_clock_apply_init_table(void)
{
	rockchip_clk_init_from_table(rk3188_clk_init_tbl, ARRAY_SIZE(rk3188_clk_init_tbl));
}

static void __init rk3188a_clk_init(struct device_node *np)
{
	void __iomem *reg_base;
	struct clk *clk, *clk2;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return;
	}

	rockchip_clk_init(np, reg_base, CLK_NR_CLKS);

	/* xin12m is created by an cru-internal divider */
	clk = clk_register_fixed_factor(NULL, "xin12m", "xin24m", 0, 1, 2);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock xin12m: %ld\n",
			__func__, PTR_ERR(clk));


	clk = clk_register_fixed_factor(NULL, "usb480m", "xin24m", 0, 20, 1);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock usb480m: %ld\n",
			__func__, PTR_ERR(clk));

	rockchip_clk_register_plls(rk3188_pll_clks,
				   ARRAY_SIZE(rk3188_pll_clks),
				   RK3188_GRF_SOC_STATUS);
	rockchip_clk_register_branches(rk3188_clk_branches,
				  ARRAY_SIZE(rk3188_clk_branches));
	rockchip_clk_register_armclk(ARMCLK, "armclk", mux_armclk_p,
				     ARRAY_SIZE(mux_armclk_p), reg_base, np);

	rockchip_register_softrst(np, 9, reg_base + RK2928_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	/* always reparent aclk_cpu_pre to gpll */
	clk = __clk_lookup("aclk_cpu_pre");
	clk2 = __clk_lookup("gpll");
	if (!IS_ERR(clk) && ! IS_ERR(clk2)) {
		if (clk_set_parent(clk, clk2)) {
			pr_warn("%s: Failed to set parent gpll of aclk_cpu_pre\n",
			       __func__);
		}
	}

	rockchip_clk_apply_init_table = rk3188_clock_apply_init_table;
}
CLK_OF_DECLARE(rk3188a_cru, "rockchip,rk3188a-cru", rk3188a_clk_init);

static void __init rk3188_clk_init(struct device_node *np)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rk3188_pll_clks); i++) {
		struct rockchip_pll_clock *pll = &rk3188_pll_clks[i];
		struct rockchip_pll_rate_table *rate;

		if (!pll->rate_table)
			continue;

		rate = pll->rate_table;
		while (rate->rate > 0) {
			rate->bwadj = 0;
			rate++;
		}
	}

	rk3188a_clk_init(np);
}
CLK_OF_DECLARE(rk3188_cru, "rockchip,rk3188-cru", rk3188_clk_init);
