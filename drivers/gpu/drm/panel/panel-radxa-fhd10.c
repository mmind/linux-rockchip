// SPDX-License-Identifier: GPL-2.0
/*
 * Radxa 10in FHD MIPI-DSI panel driver
 * Copyright (C) 2023 Theobroma Systems Design und Consulting GmbH
 *
 * based on
 *
 * Rockteck jh057n00900 5.5" MIPI-DSI panel driver
 * Copyright (C) Purism SPC 2019
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/display_timing.h>
#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct radxa_fhd10 {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct regulator *vcc;
	enum drm_panel_orientation orientation;
};

static inline struct radxa_fhd10 *panel_to_radxa_fhd10(struct drm_panel *panel)
{
	return container_of(panel, struct radxa_fhd10, panel);
}

static int radxa_fhd10_unprepare(struct drm_panel *panel)
{
	struct radxa_fhd10 *ctx = panel_to_radxa_fhd10(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		dev_err(ctx->dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to enter sleep mode: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	regulator_disable(ctx->vcc);

	return 0;
}

static int radxa_fhd10_prepare(struct drm_panel *panel)
{
	struct radxa_fhd10 *ctx = panel_to_radxa_fhd10(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	dev_dbg(ctx->dev, "Resetting the panel\n");
	ret = regulator_enable(ctx->vcc);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to enable vcc supply: %d\n", ret);
		return ret;
	}

	msleep(20);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10, 20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	msleep(20);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to exit sleep mode: %d\n", ret);
		goto disable_vcc;
	}

	msleep(250);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to set display on: %d\n", ret);
		goto disable_vcc;
	}

	msleep(50);

	return 0;

disable_vcc:
	regulator_disable(ctx->vcc);
	return ret;
}

static const struct drm_display_mode default_mode = {
	.hdisplay	= 1200,
	.hsync_start	= 1200 + 80,
	.hsync_end	= 1200 + 80 + 4,
	.htotal		= 1200 + 80 + 4 + 60,
	.vdisplay	= 1920,
	.vsync_start	= 1920 + 35,
	.vsync_end	= 1920 + 35 + 4,
	.vtotal		= 1920 + 35 + 4 + 25,
	.clock		= 160000000,
	.width_mm	= 135,
	.height_mm	= 216,
};

static int radxa_fhd10_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct radxa_fhd10 *ctx = panel_to_radxa_fhd10(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(ctx->dev, "Failed to add mode %ux%u@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static enum drm_panel_orientation radxa_fhd10_get_orientation(struct drm_panel *panel)
{
	struct radxa_fhd10 *ctx = panel_to_radxa_fhd10(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs radxa_fhd10_funcs = {
	.unprepare	= radxa_fhd10_unprepare,
	.prepare	= radxa_fhd10_prepare,
	.get_modes	= radxa_fhd10_get_modes,
	.get_orientation = radxa_fhd10_get_orientation,
};

static int radxa_fhd10_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct radxa_fhd10 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset gpio\n");
		return PTR_ERR(ctx->reset_gpio);
	}

	ctx->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(ctx->vcc)) {
		ret = PTR_ERR(ctx->vcc);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to request vcc regulator: %d\n", ret);
		return ret;
	}

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET;

	drm_panel_init(&ctx->panel, &dsi->dev, &radxa_fhd10_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach failed: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void radxa_fhd10_remove(struct mipi_dsi_device *dsi)
{
	struct radxa_fhd10 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id radxa_fhd10_of_match[] = {
	{ .compatible = "radxa,fhd10" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, radxa_fhd10_of_match);

static struct mipi_dsi_driver radxa_fhd10_driver = {
	.driver = {
		.name = "panel-radxa_fhd10",
		.of_match_table = radxa_fhd10_of_match,
	},
	.probe	= radxa_fhd10_probe,
	.remove = radxa_fhd10_remove,
};
module_mipi_dsi_driver(radxa_fhd10_driver);

MODULE_AUTHOR("Heiko Stuebner <heiko.stuebner@cherry.de>");
MODULE_DESCRIPTION("DRM driver for Radxa FHD10 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
