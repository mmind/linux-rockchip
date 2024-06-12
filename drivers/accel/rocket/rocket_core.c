// SPDX-License-Identifier: GPL-2.0
/* Copyright 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include <asm-generic/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/reset.h>

#include "rocket_core.h"
#include "rocket_device.h"
#include "rocket_registers.h"

static int rocket_clk_init(struct rocket_core *core)
{
	struct rocket_device *rdev = core->dev;
	int err;

	core->a_clk = devm_clk_get_enabled(rdev->dev, rdev->comp->clocks_a_names[core->index]);
	if (IS_ERR(core->a_clk)) {
		err = PTR_ERR(core->a_clk);
		dev_err(rdev->dev, "devm_clk_get_enabled failed %d for core %d\n", err, core->index);
		return err;
	}

	core->h_clk = devm_clk_get_enabled(rdev->dev, rdev->comp->clocks_h_names[core->index]);
	if (IS_ERR(core->h_clk)) {
		err = PTR_ERR(core->h_clk);
		dev_err(rdev->dev, "devm_clk_get_enabled failed %d for core %d\n", err, core->index);
		clk_disable_unprepare(core->a_clk);
		return err;
	}

	return 0;
}

static int rocket_reset_init(struct rocket_core *core)
{
	struct rocket_device *rdev = core->dev;
	struct reset_control *a_reset = NULL;
	struct reset_control *h_reset = NULL;

	a_reset = devm_reset_control_get(
		rdev->dev,
		rdev->comp->resets_a_names[core->index]);
	if (IS_ERR(a_reset))
		return PTR_ERR(a_reset);

	core->a_reset = a_reset;

	h_reset = devm_reset_control_get(
		rdev->dev,
		rdev->comp->resets_h_names[core->index]);
	if (IS_ERR(h_reset))
		return PTR_ERR(h_reset);

	core->h_reset = h_reset;

	return 0;
}

static int rocket_pmdomain_init(struct rocket_core *core)
{
	struct rocket_device *rdev = core->dev;
	const char *pm_domain_name = rdev->comp->pm_domain_names[core->index];
	int err = 0;

	core->pm_domain = dev_pm_domain_attach_by_name(rdev->dev, pm_domain_name);
	if (IS_ERR_OR_NULL(core->pm_domain)) {
		err = PTR_ERR(core->pm_domain) ? : -ENODATA;
		core->pm_domain = NULL;
		dev_err(rdev->dev,
			"failed to get pm-domain %s(%d): %d\n",
			pm_domain_name, core->index, err);
		return err;
	}

	core->pm_domain_link = device_link_add(rdev->dev,
			core->pm_domain,
			DL_FLAG_PM_RUNTIME | DL_FLAG_STATELESS | DL_FLAG_RPM_ACTIVE);
	if (!core->pm_domain_link) {
		dev_err(core->pm_domain, "adding device link failed!\n");
		dev_pm_domain_detach(core->pm_domain, true);
		return -ENODEV;
	}

	return err;
}

static void rocket_pmdomain_fini(struct rocket_core *core)
{
	dev_pm_domain_detach(core->pm_domain, true);
}

int rocket_core_init(struct rocket_core *core)
{
	struct rocket_device *rdev = core->dev;
	uint32_t version;
	int err = 0;

	err = rocket_clk_init(core);
	if (err) {
		dev_err(rdev->dev, "clk init failed %d\n", err);
		return err;
	}

	err = rocket_reset_init(core);
	if (err) {
		dev_err(rdev->dev, "reset init failed %d\n", err);
		return err;
	}

	err = rocket_pmdomain_init(core);
	if (err < 0)
		return err;

	core->iomem = devm_platform_ioremap_resource(rdev->pdev, core->index);
	if (IS_ERR(core->iomem)) {
		err = PTR_ERR(core->iomem);
		goto out_pm_domain;
	}

	version = rocket_read(core, REG_PC_VERSION) + (rocket_read(core, REG_PC_VERSION_NUM) & 0xffff);
	dev_info(rdev->dev, "Rockchip NPU core %d version: %d\n", core->index, version);

	return 0;

out_pm_domain:
	rocket_pmdomain_fini(core);
	return err;
}

void rocket_core_fini(struct rocket_core *core)
{
	rocket_pmdomain_fini(core);
}

void rocket_core_reset(struct rocket_core *core)
{
	reset_control_assert(core->a_reset);
	reset_control_assert(core->h_reset);

	udelay(10);

	reset_control_deassert(core->a_reset);
	reset_control_deassert(core->h_reset);
}
