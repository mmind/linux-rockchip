/*
 * Device Tree support for Rockchip SoCs
 *
 * Copyright (c) 2013 MundoReader S.L.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/irqchip.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/hardware/cache-l2x0.h>
#include "core.h"

#include <linux/clocksource.h>
#include <linux/clk-provider.h>

struct rockchip_soc_type {
	u32 reg1;
	u32 reg2;
	u32 reg3;
	u32 reg4;
};

struct rockchip_soc {
	const char *name;
	struct rockchip_soc_type type;
};

struct rockchip_soc socs[] = {
	{ "RK30xx",  { 0x33303041, 0x32303131, 0x31313131, 0x56313031 } },
	{ "RK3066b", { 0x33303041, 0x32303131, 0x31313131, 0x56313030 } },
	{ "RK3066b", { 0x33303042, 0x32303132, 0x31303031, 0x56313030 } },
	{ "RK3188",  { 0x33313042, 0x32303132, 0x31313330, 0x56313030 } },
	{ "RK3188+", { 0x33313042, 0x32303133, 0x30313331, 0x56313031 } },
	{ "unknown", { 0, 0, 0, 0 } },
};

static void __init rockchip_identify_soc(void)
{
	void __iomem *rom = ioremap(0x10120000, 0x4000);
	struct rockchip_soc_type soc;
	int i;

	soc.reg1 = readl_relaxed(rom + 0x27f0);
	soc.reg2 = readl_relaxed(rom + 0x27f4);
	soc.reg3 = readl_relaxed(rom + 0x27f8);
	soc.reg4 = readl_relaxed(rom + 0x27fc);

	pr_info("Rockchip-ID: 0x%x 0x%x 0x%x 0x%x\n", soc.reg1, soc.reg2, soc.reg3, soc.reg4);

	for (i = 0; i < ARRAY_SIZE(socs); i++) {
		if ((soc.reg1 == socs[i].type.reg1 &&
		     soc.reg2 == socs[i].type.reg2 &&
		     soc.reg3 == socs[i].type.reg3 &&
		     soc.reg4 == socs[i].type.reg4) ||
		    socs[i].type.reg1 == 0) {
			pr_info("Rockchip SoC %s\n", socs[i].name);
			break;
		}
	}
}

//extern int cclk_summary_show(void *data);

static void __init rockchip_timer_init(void)
{
	rockchip_identify_soc();
	of_clk_init(NULL);
	clocksource_of_init();

//cclk_summary_show(NULL);
}

static void __init rockchip_dt_init(void)
{
	struct platform_device_info devinfo = { .name = "cpufreq-cpu0", };

	rockchip_clocks_apply_init_table();

	l2x0_of_init(0, ~0UL);

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	platform_device_register_full(&devinfo);
}

static const char * const rockchip_board_dt_compat[] = {
	"rockchip,rk2928",
	"rockchip,rk3066a",
	"rockchip,rk3066b",
	"rockchip,rk3188",
	NULL,
};

DT_MACHINE_START(ROCKCHIP_DT, "Rockchip Cortex-A9 (Device Tree)")
	.smp		= smp_ops(rockchip_smp_ops),
	.init_machine	= rockchip_dt_init,
	.init_time	= rockchip_timer_init,
	.dt_compat	= rockchip_board_dt_compat,
MACHINE_END
