/*
 * Rockchip Successive Approximation Register (SAR) A/D Converter
 * Copyright (C) 2013-2014 ROCKCHIP, Inc.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>

#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>

#define SARADC_DATA			0x00
#define SARADC_DATA_MASK		0x3ff

#define SARADC_STAS			0x04
#define SARADC_STAS_BUSY		(1<<0)

#define SARADC_CTRL			0x08
#define SARADC_CTRL_IRQ_STATUS		(1<<6)
#define SARADC_CTRL_IRQ_ENABLE		(1<<5)
#define SARADC_CTRL_POWER_CTRL		(1<<3)
#define SARADC_CTRL_CHN_MASK		0x7

#define SARADC_DLY_PU_SOC		0x0c
#define SARADC_DLY_PU_SOC_MASK		0x3f

#define TIMEOUT			(msecs_to_jiffies(100))

struct rockchip_saradc {
	void __iomem		*regs;
	struct clk		*pclk;
	struct clk		*clk;
	struct completion	completion;
	struct regulator	*vref;
	u32			vref_mv;
	u32			last_val;
};

static int rockchip_saradc_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val,
				int *val2,
				long mask)
{
	struct rockchip_saradc *info = iio_priv(indio_dev);
	unsigned long timeout;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);

		/* Select the channel to be used and Trigger conversion */
		writel_relaxed(0x08, info->regs + SARADC_DLY_PU_SOC);
		writel(SARADC_CTRL_POWER_CTRL
				| (chan->channel & SARADC_CTRL_CHN_MASK)
				| SARADC_CTRL_IRQ_ENABLE,
		      info->regs + SARADC_CTRL);

		if (!wait_for_completion_timeout(&info->completion, TIMEOUT)) {
			writel_relaxed(0, info->regs + SARADC_CTRL);
			mutex_unlock(&indio_dev->mlock);
			return -ETIMEDOUT;
		}

		*val = info->last_val;
		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = info->vref_mv;
		*val2 = 10; //chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
/*		*val = 1;
		*val2 = 0;
		return IIO_VAL_INT;*/
	default:
		break;
	}
	return -EINVAL;
}

static irqreturn_t rockchip_saradc_isr(int irq, void *dev_id)
{
	struct rockchip_saradc *info = (struct rockchip_saradc *)dev_id;

	/* Read value */
	info->last_val = readl_relaxed(info->regs + SARADC_DATA);
	info->last_val &= SARADC_DATA_MASK;

	/* clear irq & power down adc*/
	writel_relaxed(0, info->regs + SARADC_CTRL);

	complete(&info->completion);

	return IRQ_HANDLED;
}

static const struct iio_info rockchip_saradc_iio_info = {
	.read_raw = rockchip_saradc_read_raw,
	.driver_module = THIS_MODULE,
};

#define ADC_CHANNEL(_index, _id) {			\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.channel = _index,				\
	.address = _index,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.datasheet_name = _id,				\
}

static const struct iio_chan_spec rockchip_saradc_iio_channels[] = {
	ADC_CHANNEL(0, "adc0"),
	ADC_CHANNEL(1, "adc1"),
	ADC_CHANNEL(2, "adc2"),
};

static int rockchip_saradc_probe(struct platform_device *pdev)
{
	struct rockchip_saradc *info = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct iio_dev *indio_dev = NULL;
	struct resource	*mem;
	int ret = -ENODEV;
	int irq;
	u32 rate;

	if (!np)
		return ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*info));
	if (!indio_dev) {
		dev_err(&pdev->dev, "failed allocating iio device\n");
		return -ENOMEM;
	}
	info = iio_priv(indio_dev);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->regs = devm_request_and_ioremap(&pdev->dev, mem);
	if (!info->regs)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return irq;
	}

	ret = devm_request_irq(&pdev->dev, irq, rockchip_saradc_isr,
					0, dev_name(&pdev->dev), info);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq, %d\n", ret);
		return ret;
	}

	init_completion(&info->completion);
	
	info->pclk = devm_clk_get(&pdev->dev, "pclk_saradc");
	if (IS_ERR(info->pclk)) {
	    dev_err(&pdev->dev, "failed to get pclk\n");
	    return PTR_ERR(info->pclk);
	}
	
	info->clk = devm_clk_get(&pdev->dev, "saradc");
	if (IS_ERR(info->clk)) {
	    dev_err(&pdev->dev, "failed to get adc clock\n");
	    return PTR_ERR(info->clk);
	}

	info->vref = devm_regulator_get(&pdev->dev, "vref");
	if (IS_ERR(info->vref)) {
		dev_err(&pdev->dev, "failed to get regulator, %ld\n",
							PTR_ERR(info->vref));
		return PTR_ERR(info->vref);
	}

	/* use a default of 1 MHz for the converter clock */
	if(of_property_read_u32(np, "clock-frequency", &rate))
		rate = 1000000;

	ret = clk_set_rate(info->clk, rate);
	if(ret < 0) {
		dev_err(&pdev->dev, "failed to set adc clk rate, %d\n", ret);
		return ret;
	}

	ret = regulator_enable(info->vref);
	if (ret) {
	    dev_err(&pdev->dev, "failed to enable vref regulator\n");
	    return ret;
	}

	info->vref_mv = regulator_get_voltage(info->vref) / 1000;

	ret = clk_prepare_enable(info->pclk);
	if (ret) {
	    dev_err(&pdev->dev, "failed to enable pclk\n");
	    return ret;
	}

	ret = clk_prepare_enable(info->clk);
	if (ret) {
	    dev_err(&pdev->dev, "failed to enable converter clock\n");
	    return ret;
	}

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &rockchip_saradc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	indio_dev->channels = rockchip_saradc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(rockchip_saradc_iio_channels);

	ret = devm_iio_device_register(&pdev->dev, indio_dev);
	if (ret)
		goto err_iio_dev;

	platform_set_drvdata(pdev, indio_dev);

	return 0;

err_iio_dev:
	iio_device_unregister(indio_dev);
	
err_clk:
	clk_disable_unprepare(info->clk);

err_pclk:
	clk_disable_unprepare(info->pclk);
	
	return ret;
}

static int rockchip_saradc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct rockchip_saradc *info = iio_priv(indio_dev);

//	device_for_each_child(&pdev->dev, NULL,
//				rockchip_saradc_remove_devices);
	clk_disable_unprepare(info->clk);
	clk_disable_unprepare(info->pclk);
	iio_device_unregister(indio_dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rockchip_saradc_suspend(struct device *dev)
{
	//struct iio_dev *indio_dev = dev_get_drvdata(dev);
	//struct exynos_adc *info = iio_priv(indio_dev);
	int ret = 0;
	
	return ret;
}

static int rockchip_saradc_resume(struct device *dev)
{
	//struct iio_dev *indio_dev = dev_get_drvdata(dev);
	//struct rockchip_saradc *info = iio_priv(indio_dev);
	int ret = 0;

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(rockchip_saradc_pm_ops,
			rockchip_saradc_suspend,
			rockchip_saradc_resume);

static const struct of_device_id rockchip_saradc_match[] = {
	{ .compatible = "rockchip,saradc", .data = NULL},
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_saradc_match);

static struct platform_driver rockchip_saradc_driver = {
	.probe		= rockchip_saradc_probe,
	.remove		= rockchip_saradc_remove,
	.driver		= {
		.name	= "rockchip-saradc",
		.owner	= THIS_MODULE,
		.of_match_table = rockchip_saradc_match,
		.pm	= &rockchip_saradc_pm_ops,
	},
};

module_platform_driver(rockchip_saradc_driver);
