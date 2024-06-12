/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#ifndef __ROCKET_CORE_H__
#define __ROCKET_CORE_H__

#include <linux/mutex_types.h>
#include <asm/io.h>
#include <asm-generic/io.h>

#define rocket_read(core, reg) readl((core)->iomem + (reg))
#define rocket_write(core, reg, value) writel(value, (core)->iomem + (reg))

struct rocket_core {
	struct rocket_device *dev;
	unsigned int index;

	struct reset_control *a_reset;
	struct reset_control *h_reset;
	void __iomem *iomem;
	int irq;
	struct clk *a_clk;
	struct clk *h_clk;
	struct device *pm_domain;
	struct device_link *pm_domain_link;
};

int rocket_core_init(struct rocket_core *core);
void rocket_core_fini(struct rocket_core *core);
void rocket_core_reset(struct rocket_core *core);

#endif