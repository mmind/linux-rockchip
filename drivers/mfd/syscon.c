/*
 * System Control Driver
 *
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Linaro Ltd.
 *
 * Author: Dong Aisheng <dong.aisheng@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_data/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>

static struct platform_driver syscon_driver;

struct syscon {
	void __iomem *base;
	struct regmap *regmap;
	struct resource res;
};

static int syscon_match_node(struct device *dev, void *data)
{
	struct device_node *dn = data;

	return (dev->of_node == dn) ? 1 : 0;
}

struct regmap *syscon_node_to_regmap(struct device_node *np)
{
	struct syscon *syscon;
	struct device *dev;

	dev = driver_find_device(&syscon_driver.driver, NULL, np,
				 syscon_match_node);
	if (!dev)
		return ERR_PTR(-EPROBE_DEFER);

	syscon = dev_get_drvdata(dev);

	return syscon->regmap;
}
EXPORT_SYMBOL_GPL(syscon_node_to_regmap);

struct regmap *syscon_regmap_lookup_by_compatible(const char *s)
{
	struct device_node *syscon_np;
	struct regmap *regmap;

	syscon_np = of_find_compatible_node(NULL, NULL, s);
	if (!syscon_np)
		return ERR_PTR(-ENODEV);

	regmap = syscon_node_to_regmap(syscon_np);
	of_node_put(syscon_np);

	return regmap;
}
EXPORT_SYMBOL_GPL(syscon_regmap_lookup_by_compatible);

static int syscon_match_pdevname(struct device *dev, void *data)
{
	return !strcmp(dev_name(dev), (const char *)data);
}

struct regmap *syscon_regmap_lookup_by_pdevname(const char *s)
{
	struct device *dev;
	struct syscon *syscon;

	dev = driver_find_device(&syscon_driver.driver, NULL, (void *)s,
				 syscon_match_pdevname);
	if (!dev)
		return ERR_PTR(-EPROBE_DEFER);

	syscon = dev_get_drvdata(dev);

	return syscon->regmap;
}
EXPORT_SYMBOL_GPL(syscon_regmap_lookup_by_pdevname);

/**
 * syscon_early_regmap_lookup_by_phandle - Early phandle lookup function
 * @np:		device_node pointer
 * @property:	property name which handle system controller phandle
 * Return:	regmap pointer, an error pointer otherwise
 */
struct regmap *syscon_early_regmap_lookup_by_phandle(struct device_node *np,
						     const char *property)
{
	struct device_node *syscon_np;
	struct syscon *syscon;

	syscon_np = of_parse_phandle(np, property, 0);
	if (!syscon_np)
		return ERR_PTR(-ENODEV);

	syscon = syscon_np->data;

	of_node_put(syscon_np);

	return syscon->regmap;
}
EXPORT_SYMBOL_GPL(syscon_early_regmap_lookup_by_phandle);

struct regmap *syscon_regmap_lookup_by_phandle(struct device_node *np,
					const char *property)
{
	struct device_node *syscon_np;
	struct regmap *regmap;

	if (property)
		syscon_np = of_parse_phandle(np, property, 0);
	else
		syscon_np = np;

	if (!syscon_np)
		return ERR_PTR(-ENODEV);

	regmap = syscon_node_to_regmap(syscon_np);
	of_node_put(syscon_np);

	return regmap;
}
EXPORT_SYMBOL_GPL(syscon_regmap_lookup_by_phandle);

static const struct of_device_id of_syscon_match[] = {
	{ .compatible = "syscon", },
	{ },
};

static struct regmap_config syscon_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

/**
 * early_syscon_probe - Early system controller probe method
 * @np:		device_node pointer
 * @syscon_p:	syscon pointer
 * @res:	device IO resource
 * Return:	0 if successful, a negative error code otherwise
 */
static int early_syscon_probe(struct device_node *np, struct syscon **syscon_p,
			      struct resource *res)
{
	struct device *dev = &pdev->dev;
	struct syscon_platform_data *pdata = dev_get_platdata(dev);
	struct syscon *syscon;
	int ret;

	if (np && np->data) {
		pr_debug("Early syscon was called\n");
		*syscon_p = (struct syscon *)&np->data;
		return 0;
	}

	syscon = kzalloc(sizeof(*syscon), GFP_KERNEL);
	if (!syscon)
		return -ENOMEM;

	*syscon_p = (struct syscon *)&syscon;

	if (!res && np) {
		if (of_address_to_resource(np, 0, &syscon->res)) {
			ret = -EINVAL;
			goto alloc;
		}

		np->data = syscon;
		of_node_put(np);
	} else {
		syscon->res = *res;
	}

	syscon->base = ioremap(syscon->res.start, resource_size(&syscon->res));
	if (!syscon->base) {
		pr_err("%s: Unable to map I/O memory\n", __func__);
		ret = PTR_ERR(syscon->base);
		goto alloc;
	}

	syscon_regmap_config.max_register = syscon->res.end -
					    syscon->res.start - 3;
	if (pdata)
		syscon_regmap_config.name = pdata->label;
	syscon->regmap = regmap_init_mmio(NULL, syscon->base,
					  &syscon_regmap_config);
	if (IS_ERR(syscon->regmap)) {
		pr_err("regmap init failed\n");
		ret = PTR_ERR(syscon->regmap);
		goto iomap;
	}
	if (np)
		pr_info("syscon: %s regmap %pR registered\n", np->name,
			&syscon->res);

	return 0;

iomap:
	iounmap(syscon->base);
alloc:
	kfree(syscon);

	return ret;
}

/**
 * early_syscon_init - Early system controller initialization
 */
void __init early_syscon_init(void)
{
	struct device_node *np;
	struct syscon *syscon = NULL;

	for_each_matching_node_and_match(np, of_syscon_match, NULL) {
		if (early_syscon_probe(np, &syscon, NULL))
			BUG();
	}
}

/**
 * syscon_probe - System controller probe method
 * @pdev:	Platform device
 * Return:	0 if successful, a negative error code otherwise
 */
static int syscon_probe(struct platform_device *pdev)
{
	struct syscon *syscon, *syscon_p;
	struct resource *res = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!np) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res)
			return -ENOENT;
	}
	ret = early_syscon_probe(np, &syscon_p, res);
	if (ret) {
		dev_err(dev, "Syscon probe failed\n");
		return ret;
	}

	syscon = *(struct syscon **)syscon_p;

	regmap_attach_dev(dev, syscon->regmap, &syscon_regmap_config);

	platform_set_drvdata(pdev, syscon);

	dev_dbg(dev, "regmap attach device to %pR\n", &syscon->res);

	return 0;
}

static const struct platform_device_id syscon_ids[] = {
	{ "syscon", },
	{ }
};

/**
 * syscon_remove - System controller cleanup function
 * @pdev:	Platform device
 * Return:	0 always
 */
static int syscon_remove(struct platform_device *pdev)
{
	struct syscon *syscon = platform_get_drvdata(pdev);

	iounmap(syscon->base);
	kfree(syscon);

	return 0;
}

static struct platform_driver syscon_driver = {
	.driver = {
		.name = "syscon",
		.owner = THIS_MODULE,
		.of_match_table = of_syscon_match,
	},
	.probe		= syscon_probe,
	.remove		= syscon_remove,
	.id_table	= syscon_ids,
};

static int __init syscon_init(void)
{
	return platform_driver_register(&syscon_driver);
}
postcore_initcall(syscon_init);

static void __exit syscon_exit(void)
{
	platform_driver_unregister(&syscon_driver);
}
module_exit(syscon_exit);

MODULE_AUTHOR("Dong Aisheng <dong.aisheng@linaro.org>");
MODULE_DESCRIPTION("System Control driver");
MODULE_LICENSE("GPL v2");
