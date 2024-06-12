/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#ifndef __ROCKET_DEVICE_H__
#define __ROCKET_DEVICE_H__

#include "rocket_core.h"

#define MAX_NUM_CORES 3

struct rocket_compatible {
	int num_cores;
	const char * const *resets_a_names;
	const char * const *resets_h_names;
	const char * const *clocks_a_names;
	const char * const *clocks_h_names;
	const char * const *pm_domain_names;
	const char * const *irq_names;
};

struct rocket_device {
	struct device *dev;
	struct drm_device *ddev;
	struct platform_device *pdev;

	const struct rocket_compatible *comp;

	struct rocket_core cores[MAX_NUM_CORES];

	struct mutex sched_lock;

	struct clk *clk_npu;
	struct clk *pclk;
};

int rocket_device_init(struct rocket_device *rdev);
void rocket_device_fini(struct rocket_device *rdev);
void rocket_device_reset(struct rocket_device *rdev);

#endif
