// SPDX-License-Identifier: GPL-2.0
/* Copyright 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#include "linux/clk.h"
#include "linux/dma-mapping.h"
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <drm/drm_drv.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_accel.h>
#include <drm/drm_gem.h>
#include <drm/rocket_accel.h>

#include "rocket_drv.h"
#include "rocket_device.h"
#include "rocket_gem.h"
#include "rocket_job.h"

static const char * const rk3588_pm_domains[] = { "npu0", "npu1", "npu2" };
static const char * const rk3588_resets_a[] = { "srst_a0", "srst_a1", "srst_a2" };
static const char * const rk3588_resets_h[] = { "srst_h0", "srst_h1", "srst_h2" };
static const char * const rk3588_clocks_a[] = { "aclk0", "aclk1", "aclk2" };
static const char * const rk3588_clocks_h[] = { "hclk0", "hclk1", "hclk2" };
static const char * const rk3588_irqs[] = { "npu0_irq", "npu1_irq", "npu2_irq" };
static const struct rocket_compatible rk3588_data = {
	.num_cores = ARRAY_SIZE(rk3588_pm_domains),
	.pm_domain_names = rk3588_pm_domains,
	.resets_a_names = rk3588_resets_a,
	.resets_h_names = rk3588_resets_h,
	.clocks_a_names = rk3588_clocks_a,
	.clocks_h_names = rk3588_clocks_h,
	.irq_names = rk3588_irqs,
};

static int
rocket_open(struct drm_device *dev, struct drm_file *file)
{
	struct rocket_device *rdev = dev->dev_private;
	struct rocket_file_priv *rocket_priv;
	int ret;

	rocket_priv = kzalloc(sizeof(*rocket_priv), GFP_KERNEL);
	if (!rocket_priv)
		return -ENOMEM;

	rocket_priv->rdev = rdev;
	file->driver_priv = rocket_priv;

	ret = rocket_job_open(rocket_priv);
	if (ret)
		goto err_free;

	return 0;

err_free:
	kfree(rocket_priv);
	return ret;
}

static void
rocket_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct rocket_file_priv *rocket_priv = file->driver_priv;

	rocket_job_close(rocket_priv);
	kfree(rocket_priv);
}

static const struct drm_ioctl_desc rocket_drm_driver_ioctls[] = {
#define ROCKET_IOCTL(n, func) \
	DRM_IOCTL_DEF_DRV(ROCKET_##n, rocket_ioctl_##func, 0)

	ROCKET_IOCTL(CREATE_BO, create_bo),
	ROCKET_IOCTL(SUBMIT, submit),
};

static const struct file_operations rocket_drm_driver_fops = {
	.owner = THIS_MODULE,
	DRM_ACCEL_FOPS,
};

/*
 * Rocket driver version:
 * - 1.0 - initial interface
 */
static const struct drm_driver rocket_drm_driver = {
	.driver_features	= DRIVER_COMPUTE_ACCEL | DRIVER_GEM,
	.open			= rocket_open,
	.postclose		= rocket_postclose,
	.gem_create_object	= rocket_gem_create_object,
	.ioctls			= rocket_drm_driver_ioctls,
	.num_ioctls		= ARRAY_SIZE(rocket_drm_driver_ioctls),
	.fops			= &rocket_drm_driver_fops,
	.name			= "rocket",
	.desc			= "rocket DRM",
	.date			= "20240521",
	.major			= 1,
	.minor			= 0,
};

static int rocket_probe(struct platform_device *pdev)
{
	struct rocket_device *rdev;
	struct drm_device *ddev;
	int err;

	BUILD_BUG_ON(rk3588_data.num_cores > MAX_NUM_CORES);

	rdev = devm_kzalloc(&pdev->dev, sizeof(*rdev), GFP_KERNEL);
	if (!rdev)
		return -ENOMEM;

	rdev->pdev = pdev;
	rdev->dev = &pdev->dev;

	platform_set_drvdata(pdev, rdev);

	rdev->comp = of_device_get_match_data(&pdev->dev);
	if (!rdev->comp)
		return -ENODEV;

	/* Allocate and initialize the DRM device. */
	ddev = drm_dev_alloc(&rocket_drm_driver, &pdev->dev);
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);

	ddev->dev_private = rdev;
	rdev->ddev = ddev;

	err = dma_set_mask_and_coherent(rdev->ddev->dev, DMA_BIT_MASK(40));
	if (err)
		goto err_drm_dev;

	err = rocket_device_init(rdev);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Fatal error during NPU init\n");
		goto err_drm_dev;
	}

	pm_runtime_use_autosuspend(rdev->dev);
	pm_runtime_set_autosuspend_delay(rdev->dev, 50); /* ~3 frames */
	pm_runtime_set_active(rdev->dev);
	pm_runtime_enable(rdev->dev);
	pm_runtime_mark_last_busy(rdev->dev);

	/*
	 * Register the DRM device with the core and the connectors with
	 * sysfs
	 */
	err = drm_dev_register(ddev, 0);
	if (err < 0)
		goto err_pm_runtime;

	return 0;

err_pm_runtime:
	pm_runtime_disable(rdev->dev);
	rocket_device_fini(rdev);
	pm_runtime_set_suspended(rdev->dev);
err_drm_dev:
	drm_dev_put(ddev);
	return err;
}

static void rocket_remove(struct platform_device *pdev)
{
	struct rocket_device *rdev = platform_get_drvdata(pdev);
	struct drm_device *ddev = rdev->ddev;

	drm_dev_unregister(ddev);

	pm_runtime_disable(rdev->dev);
	rocket_device_fini(rdev);
	pm_runtime_set_suspended(rdev->dev);

	drm_dev_put(ddev);
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "rockchip,rk3588-rknn", .data = &rk3588_data, },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static int rocket_device_runtime_resume(struct device *dev)
{
	struct rocket_device *rdev = dev_get_drvdata(dev);
	int core;

	clk_prepare_enable(rdev->clk_npu);
	clk_prepare_enable(rdev->pclk);

	for (core = 0; core < rdev->comp->num_cores; core++) {
		clk_prepare_enable(rdev->cores[core].a_clk);
		clk_prepare_enable(rdev->cores[core].h_clk);
	}

	return 0;
}

static int rocket_device_runtime_suspend(struct device *dev)
{
	struct rocket_device *rdev = dev_get_drvdata(dev);
	int core;

	if (!rocket_job_is_idle(rdev))
		return -EBUSY;

	for (core = 0; core < rdev->comp->num_cores; core++) {
		clk_disable_unprepare(rdev->cores[core].a_clk);
		clk_disable_unprepare(rdev->cores[core].h_clk);
	}

	clk_disable_unprepare(rdev->pclk);
	clk_disable_unprepare(rdev->clk_npu);

	return 0;
}

EXPORT_GPL_DEV_PM_OPS(rocket_pm_ops) = {
	RUNTIME_PM_OPS(rocket_device_runtime_suspend, rocket_device_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};

static struct platform_driver rocket_driver = {
	.probe = rocket_probe,
	.remove_new = rocket_remove,
	.driver	 = {
		.name = "rocket",
		.pm = pm_ptr(&rocket_pm_ops),
		.of_match_table = dt_match,
	},
};
module_platform_driver(rocket_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DRM driver for the Rockchip NPU IP");
MODULE_AUTHOR("Tomeu Vizoso");
