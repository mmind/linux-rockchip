// SPDX-License-Identifier: GPL-2.0
/* Copyright 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include <linux/clk.h>
#include <linux/pm_runtime.h>

#include "rocket_drv.h"
#include "rocket_device.h"

int rocket_device_init(struct rocket_device *rdev)
{
	int core, err;

	rdev->clk_npu = devm_clk_get_enabled(rdev->dev, "clk_npu");
	rdev->pclk = devm_clk_get_enabled(rdev->dev, "pclk");

	for (core = 0; core < rdev->comp->num_cores; core++) {
		rdev->cores[core].dev = rdev;
		rdev->cores[core].index = core;

		err = rocket_core_init(&rdev->cores[core]);
		if (err) {
			rocket_device_fini(rdev);
			return err;
		}
	}

	return 0;
}

void rocket_device_fini(struct rocket_device *rdev)
{
	int core;

	for (core = 0; core < rdev->comp->num_cores; core++)
		rocket_core_fini(&rdev->cores[core]);
}
