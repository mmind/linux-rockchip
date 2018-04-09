/**
 * dwc3-rockchip-inno.c - Rockchip DWC3 Specific Glue layer with INNO PHY
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 *
 * Authors: William Wu <william.wu@rock-chips.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/notifier.h>
#include <linux/usb/phy.h>

#include "core.h"
#include "../host/xhci.h"

struct dwc3_rockchip {
	struct device		*dev;
	struct clk		**clks;
	int			num_clocks;
	struct reset_control	*resets;

	struct platform_device	*dwc_pdev;
	struct usb_phy		*phy;
	struct notifier_block	u3phy_nb;
	struct work_struct	u3_work;
	struct mutex		lock;
};

static int u3phy_disconnect_det_notifier(struct notifier_block *nb,
					 unsigned long event, void *p)
{
	struct dwc3_rockchip *rockchip =
		container_of(nb, struct dwc3_rockchip, u3phy_nb);

	schedule_work(&rockchip->u3_work);

	return NOTIFY_DONE;
}

static void u3phy_disconnect_det_work(struct work_struct *work)
{
	struct dwc3_rockchip *rockchip =
		container_of(work, struct dwc3_rockchip, u3_work);
	struct dwc3 *dwc;
	struct usb_hcd	*hcd;
	struct usb_hcd	*shared_hcd;
	struct xhci_hcd	*xhci;
	u32 count = 0;

	mutex_lock(&rockchip->lock);

	dwc = platform_get_drvdata(rockchip->dwc_pdev);
	if (!dwc || !dwc->xhci) {
		dev_err(rockchip->dev,
			"failed to get dwc3 drvdata handling disconnect\n");
		mutex_unlock(&rockchip->lock);
		return;
	}

	hcd = dev_get_drvdata(&dwc->xhci->dev);
	if (!hcd) {
		dev_err(rockchip->dev,
			"failed to get hcd drvdata handling disconnect\n");
		mutex_unlock(&rockchip->lock);
		return;
	}

	shared_hcd = hcd->shared_hcd;
	xhci = hcd_to_xhci(hcd);

	if (hcd->state != HC_STATE_HALT) {
		usb_remove_hcd(shared_hcd);
		usb_remove_hcd(hcd);
	}

	usb_phy_shutdown(rockchip->phy);

	while (hcd->state != HC_STATE_HALT) {
		if (++count > 1000) {
			dev_err(rockchip->dev,
				"wait for HCD remove 1s timeout!\n");
			break;
		}
		usleep_range(1000, 1100);
	}

	if (hcd->state == HC_STATE_HALT) {
		xhci->shared_hcd = shared_hcd;
		usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
		usb_add_hcd(shared_hcd, hcd->irq, IRQF_SHARED);
	}

	usb_phy_init(rockchip->phy);

	mutex_unlock(&rockchip->lock);
}

static int dwc3_rockchip_clk_init(struct dwc3_rockchip *rockchip, int count)
{
	struct device		*dev = rockchip->dev;
	struct device_node	*np = dev->of_node;
	int			i;

	rockchip->num_clocks = count;

	if (!count)
		return 0;

	rockchip->clks = devm_kcalloc(dev, rockchip->num_clocks,
			sizeof(struct clk *), GFP_KERNEL);
	if (!rockchip->clks)
		return -ENOMEM;

	for (i = 0; i < rockchip->num_clocks; i++) {
		struct clk	*clk;
		int		ret;

		clk = of_clk_get(np, i);
		if (IS_ERR(clk)) {
			while (--i >= 0) {
				clk_disable_unprepare(rockchip->clks[i]);
				clk_put(rockchip->clks[i]);
			}
			return PTR_ERR(clk);
		}

		ret = clk_prepare_enable(clk);
		if (ret < 0) {
			while (--i >= 0) {
				clk_disable_unprepare(rockchip->clks[i]);
				clk_put(rockchip->clks[i]);
			}
			clk_put(clk);

			return ret;
		}

		rockchip->clks[i] = clk;
	}

	return 0;
}

static int dwc3_rockchip_probe(struct platform_device *pdev)
{
	struct dwc3_rockchip	*rockchip;
	struct device		*dev = &pdev->dev;
	struct device_node	*np = dev->of_node, *child;
	struct platform_device	*child_pdev;

	int			ret;
	int			i;

	rockchip = devm_kzalloc(dev, sizeof(*rockchip), GFP_KERNEL);
	if (!rockchip)
		return -ENOMEM;

	platform_set_drvdata(pdev, rockchip);
	rockchip->dev = dev;

	rockchip->phy = devm_usb_get_phy(dev, USB_PHY_TYPE_USB3);
	if (IS_ERR(rockchip->phy)) {
		if (PTR_ERR(rockchip->phy) == -ENODEV)
			rockchip->phy = NULL;
		else
			return PTR_ERR(rockchip->phy);
	}

	rockchip->resets = of_reset_control_array_get(np, false, true);
	if (IS_ERR(rockchip->resets)) {
		ret = PTR_ERR(rockchip->resets);
		dev_err(dev, "failed to get device resets, err=%d\n", ret);
		return ret;
	}

	ret = reset_control_deassert(rockchip->resets);
	if (ret)
		goto err_resetc_put;

	ret = dwc3_rockchip_clk_init(rockchip, of_count_phandle_with_args(np,
						"clocks", "#clock-cells"));
	if (ret)
		goto err_resetc_assert;

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret)
		goto err_disable_clk;

	child = of_get_child_by_name(np, "dwc3");
	if (!child) {
		dev_err(dev, "failed to find dwc3 core node\n");
		ret = -ENODEV;
		goto err_depopulate;
	}

	rockchip->dwc_pdev = of_find_device_by_node(child);
	of_node_put(child);
	if (!rockchip->dwc_pdev) {
		dev_err(dev, "failed to find dwc3 core device\n");
		ret = -ENODEV;
		goto err_depopulate;
	}

	if (rockchip->phy) {
		mutex_init(&rockchip->lock);
		INIT_WORK(&rockchip->u3_work, u3phy_disconnect_det_work);
		rockchip->u3phy_nb.notifier_call =
			u3phy_disconnect_det_notifier;
		usb_register_notifier(rockchip->phy, &rockchip->u3phy_nb);
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	return 0;

err_depopulate:
	of_platform_depopulate(dev);
err_disable_clk:
	for (i = 0; i < rockchip->num_clocks && rockchip->clks[i]; i++) {
		clk_disable_unprepare(rockchip->clks[i]);
		clk_put(rockchip->clks[i]);
	}
err_resetc_assert:
	reset_control_assert(rockchip->resets);
err_resetc_put:
	reset_control_put(rockchip->resets);
	return ret;
}

static int dwc3_rockchip_remove(struct platform_device *pdev)
{
	struct dwc3_rockchip	*rockchip = platform_get_drvdata(pdev);
	struct device		*dev = &pdev->dev;
	int			i;

	of_platform_depopulate(dev);

	for (i = 0; i < rockchip->num_clocks; i++) {
		clk_disable_unprepare(rockchip->clks[i]);
		clk_put(rockchip->clks[i]);
	}
	rockchip->num_clocks = 0;

	reset_control_assert(rockchip->resets);
	reset_control_put(rockchip->resets);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return 0;
}

#ifdef CONFIG_PM
static int dwc3_rockchip_runtime_suspend(struct device *dev)
{
	struct dwc3_rockchip	*rockchip = dev_get_drvdata(dev);
	int			i;

	for (i = 0; i < rockchip->num_clocks; i++)
		clk_disable(rockchip->clks[i]);

	return 0;
}

static int dwc3_rockchip_runtime_resume(struct device *dev)
{
	struct dwc3_rockchip	*rockchip = dev_get_drvdata(dev);
	int			ret;
	int			i;

	for (i = 0; i < rockchip->num_clocks; i++) {
		ret = clk_enable(rockchip->clks[i]);
		if (ret < 0) {
			while (--i >= 0)
				clk_disable(rockchip->clks[i]);
			return ret;
		}
	}

	return 0;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops dwc3_rockchip_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(dwc3_rockchip_runtime_suspend,
			   dwc3_rockchip_runtime_resume, NULL)
};

static const struct of_device_id rockchip_dwc3_match[] = {
	{ .compatible = "rockchip,rk3328-dwc3" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, rockchip_dwc3_match);

static struct platform_driver dwc3_rockchip_driver = {
	.probe		= dwc3_rockchip_probe,
	.remove		= dwc3_rockchip_remove,
	.driver		= {
		.name	= "rockchip-inno-dwc3",
		.pm	= &dwc3_rockchip_dev_pm_ops,
		.of_match_table = rockchip_dwc3_match,
	},
};

module_platform_driver(dwc3_rockchip_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 rockchip-inno Glue Layer");
MODULE_AUTHOR("William Wu <william.wu@rock-chips.com>");
