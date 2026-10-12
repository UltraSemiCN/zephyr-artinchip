/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT fitipower_ek79007ad

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mipi_dsi.h>
#include <zephyr/drivers/mipi_dsi/artinchip_dsi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(display_ek79007ad, CONFIG_DISPLAY_LOG_LEVEL);

struct ek79007ad_config {
	const struct device *mipi_dsi;
	struct gpio_dt_spec reset;
	struct gpio_dt_spec backlight;
	uint8_t data_lanes;
	uint8_t channel;
	uint32_t pixel_format;
	uint16_t width;
	uint16_t height;
	struct mipi_dsi_timings timings;
};

static int ek79007ad_write(const struct device *dev, uint8_t cmd, const uint8_t *data,
			   size_t len)
{
	const struct ek79007ad_config *config = dev->config;
	struct mipi_dsi_msg msg = {
		.cmd = cmd,
		.tx_buf = data,
		.tx_len = len,
		.flags = MIPI_DSI_MSG_USE_LPM,
	};
	ssize_t ret;

	switch (len) {
	case 0U:
		msg.type = MIPI_DSI_DCS_SHORT_WRITE;
		break;
	case 1U:
		msg.type = MIPI_DSI_DCS_SHORT_WRITE_PARAM;
		break;
	default:
		msg.type = MIPI_DSI_DCS_LONG_WRITE;
		break;
	}

	ret = mipi_dsi_transfer(config->mipi_dsi, config->channel, &msg);
	if (ret < 0) {
		return (int)ret;
	}

	return 0;
}

static int ek79007ad_write_u8(const struct device *dev, uint8_t cmd, uint8_t value)
{
	return ek79007ad_write(dev, cmd, &value, 1U);
}

static int ek79007ad_init_sequence(const struct device *dev)
{
	int ret;

	ret = ek79007ad_write(dev, 0x01, NULL, 0U);
	if (ret != 0) {
		return ret;
	}
	k_msleep(20);

	ret = ek79007ad_write_u8(dev, 0xb0, 0x80);
	if (ret != 0) {
		return ret;
	}
	k_msleep(10);

	ret = ek79007ad_write_u8(dev, 0xb1, 0x00);
	if (ret != 0) {
		return ret;
	}
	k_msleep(10);

	ret = ek79007ad_write_u8(dev, 0xb2, 0x00);
	if (ret != 0) {
		return ret;
	}
	k_msleep(10);

	ret = ek79007ad_write_u8(dev, 0xb3, 0x00);
	if (ret != 0) {
		return ret;
	}
	k_msleep(10);

	ret = ek79007ad_write_u8(dev, 0x36, 0x01);
	if (ret != 0) {
		return ret;
	}
	k_msleep(10);

	ret = ek79007ad_write(dev, MIPI_DCS_EXIT_SLEEP_MODE, NULL, 0U);
	if (ret != 0) {
		return ret;
	}
	k_msleep(120);

	ret = ek79007ad_write(dev, MIPI_DCS_SET_DISPLAY_ON, NULL, 0U);
	if (ret != 0) {
		return ret;
	}
	k_msleep(20);

	return 0;
}

static int ek79007ad_panel_reset(const struct device *dev)
{
	const struct ek79007ad_config *config = dev->config;
	int ret;

	if (config->reset.port == NULL) {
		return 0;
	}

	if (!gpio_is_ready_dt(&config->reset)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->reset, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		return ret;
	}

	k_msleep(1);
	ret = gpio_pin_set_dt(&config->reset, 1);
	if (ret != 0) {
		return ret;
	}

	k_msleep(50);
	return 0;
}

static int ek79007ad_attach_and_init(const struct device *dev)
{
	const struct ek79007ad_config *config = dev->config;
	int ret;

	ret = artinchip_dsi_set_videomode(config->mipi_dsi, false);
	if (ret != 0) {
		return ret;
	}

	ret = ek79007ad_init_sequence(dev);
	if (ret != 0) {
		return ret;
	}

	ret = artinchip_dsi_set_videomode(config->mipi_dsi, true);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

int ek79007ad_backlight_enable(const struct device *dev)
{
	const struct ek79007ad_config *config = dev->config;

	if (config->backlight.port == NULL) {
		return 0;
	}

	return gpio_pin_set_dt(&config->backlight, 1);
}

int ek79007ad_attach_dsi(const struct device *dev)
{
	const struct ek79007ad_config *config = dev->config;
	struct mipi_dsi_device mdev = {
		.data_lanes = config->data_lanes,
		.timings = config->timings,
		.pixfmt = config->pixel_format,
		.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			      MIPI_DSI_MODE_LPM,
	};

	return mipi_dsi_attach(config->mipi_dsi, config->channel, &mdev);
}

int ek79007ad_bringup(const struct device *dev)
{
	int ret;

	ret = ek79007ad_panel_reset(dev);
	if (ret != 0) {
		return ret;
	}

	ret = ek79007ad_attach_and_init(dev);
	if (ret != 0) {
		LOG_ERR("panel bring-up failed: %d", ret);
	}

	return ret;
}

static int ek79007ad_blanking_on(const struct device *dev)
{
	const struct ek79007ad_config *config = dev->config;
	int ret;

	if (config->backlight.port != NULL) {
		ret = gpio_pin_set_dt(&config->backlight, 0);
		if (ret != 0) {
			return ret;
		}
	}

	return ek79007ad_write(dev, MIPI_DCS_SET_DISPLAY_OFF, NULL, 0U);
}

static int ek79007ad_blanking_off(const struct device *dev)
{
	const struct ek79007ad_config *config = dev->config;
	int ret;

	ret = ek79007ad_write(dev, MIPI_DCS_SET_DISPLAY_ON, NULL, 0U);
	if (ret != 0) {
		return ret;
	}

	if (config->backlight.port != NULL) {
		return gpio_pin_set_dt(&config->backlight, 1);
	}

	return 0;
}

static void ek79007ad_get_capabilities(const struct device *dev,
				       struct display_capabilities *capabilities)
{
	const struct ek79007ad_config *config = dev->config;

	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->x_resolution = config->width;
	capabilities->y_resolution = config->height;
	capabilities->supported_pixel_formats = PIXEL_FORMAT_RGB_888;
	capabilities->current_pixel_format = PIXEL_FORMAT_RGB_888;
	capabilities->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static int ek79007ad_set_pixel_format(const struct device *dev,
				      enum display_pixel_format pixel_format)
{
	ARG_UNUSED(dev);

	return (pixel_format == PIXEL_FORMAT_RGB_888) ? 0 : -ENOTSUP;
}

static DEVICE_API(display, ek79007ad_api) = {
	.blanking_on = ek79007ad_blanking_on,
	.blanking_off = ek79007ad_blanking_off,
	.get_capabilities = ek79007ad_get_capabilities,
	.set_pixel_format = ek79007ad_set_pixel_format,
};

static int ek79007ad_init(const struct device *dev)
{
	const struct ek79007ad_config *config = dev->config;
	int ret;

	if (!device_is_ready(config->mipi_dsi)) {
		return -ENODEV;
	}

	if (config->backlight.port != NULL) {
		if (!gpio_is_ready_dt(&config->backlight)) {
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->backlight, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			return ret;
		}
	}

#if IS_ENABLED(CONFIG_EK79007AD_DEFER_PANEL_INIT)
	return 0;
#else
	ret = ek79007ad_panel_reset(dev);
	if (ret != 0) {
		return ret;
	}

	ret = ek79007ad_attach_and_init(dev);
	if (ret != 0) {
		LOG_ERR("panel init sequence failed: %d", ret);
		return ret;
	}

	return 0;
#endif
}

#define EK79007AD_DEFINE(inst)									\
	static const struct ek79007ad_config ek79007ad_config_##inst = {			\
		.mipi_dsi = DEVICE_DT_GET(DT_INST_BUS(inst)),				\
		.reset = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),		\
		.backlight = GPIO_DT_SPEC_INST_GET_OR(inst, bl_gpios, {0}),		\
		.data_lanes = DT_INST_PROP_BY_IDX(inst, data_lanes, 0),			\
		.channel = DT_INST_REG_ADDR(inst),					\
		.pixel_format = DT_INST_PROP(inst, pixel_format),			\
		.width = DT_INST_PROP(inst, width),					\
		.height = DT_INST_PROP(inst, height),					\
		.timings = {								\
			.hactive = DT_INST_PROP(inst, width),				\
			.hfp = DT_INST_PROP(inst, hfront_porch),			\
			.hbp = DT_INST_PROP(inst, hback_porch),				\
			.hsync = DT_INST_PROP(inst, hsync_len),				\
			.vactive = DT_INST_PROP(inst, height),				\
			.vfp = DT_INST_PROP(inst, vfront_porch),			\
			.vbp = DT_INST_PROP(inst, vback_porch),				\
			.vsync = DT_INST_PROP(inst, vsync_len),				\
		},									\
	};										\
	DEVICE_DT_INST_DEFINE(inst, ek79007ad_init, NULL, NULL,				\
			      &ek79007ad_config_##inst, POST_KERNEL,			\
			      CONFIG_DISPLAY_EK79007AD_INIT_PRIORITY, &ek79007ad_api);

DT_INST_FOREACH_STATUS_OKAY(EK79007AD_DEFINE)
