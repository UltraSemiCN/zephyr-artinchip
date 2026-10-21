/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_pos_panel

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mipi_dsi.h>
#include <zephyr/drivers/mipi_dsi/artinchip_dsi.h>
#include <zephyr/drivers/display/artinchip_de.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(display_artinchip_pos_panel, CONFIG_DISPLAY_LOG_LEVEL);

struct artinchip_pos_panel_config {
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

enum pos_cmd_type {
	POS_DCS,
	POS_GENERIC,
	POS_DELAY,
};

struct pos_cmd {
	uint8_t type;
	/* Payload length, or milliseconds for POS_DELAY. */
	uint8_t len;
	uint8_t data[64];
};

#include "display_pos_panel_sequences.inc"

static int artinchip_pos_panel_panel_reset(const struct device *dev);

static int artinchip_pos_panel_write(const struct device *dev, uint8_t cmd, const uint8_t *data,
			   size_t len)
{
	const struct artinchip_pos_panel_config *config = dev->config;
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

static int artinchip_pos_panel_write_u8(const struct device *dev, uint8_t cmd, uint8_t value)
{
	return artinchip_pos_panel_write(dev, cmd, &value, 1U);
}

static int pos_generic_write(const struct device *dev, const uint8_t *data, size_t len)
{
	const struct artinchip_pos_panel_config *config = dev->config;
	struct mipi_dsi_msg msg = {
		.type = len <= 2U ? (len == 1U ? MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM :
					       MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM) :
				       MIPI_DSI_GENERIC_LONG_WRITE,
		.tx_buf = data,
		.tx_len = len,
		.flags = MIPI_DSI_MSG_USE_LPM,
	};
	ssize_t ret = mipi_dsi_transfer(config->mipi_dsi, config->channel, &msg);

	return ret < 0 ? (int)ret : 0;
}

static int pos_run_sequence(const struct device *dev, const struct pos_cmd *sequence,
			    size_t count)
{
	for (size_t i = 0U; i < count; i++) {
		const struct pos_cmd *entry = &sequence[i];
		int ret;

		if (entry->type == POS_DELAY) {
			k_msleep(entry->len);
			continue;
		}

		if (entry->type == POS_GENERIC) {
			ret = pos_generic_write(dev, entry->data, entry->len + 1U);
		} else {
			ret = artinchip_pos_panel_write(dev, entry->data[0], &entry->data[1], entry->len);
		}
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int pos_read_panel_id(const struct device *dev, uint16_t *panel_id)
{
	const struct artinchip_pos_panel_config *config = dev->config;
	uint8_t max_return[] = { 2U, 0U };
	uint8_t response[2] = { 0U, 0U };
	struct mipi_dsi_msg max_msg = {
		.type = MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE,
		.tx_buf = max_return,
		.tx_len = sizeof(max_return),
		.flags = MIPI_DSI_MSG_USE_LPM,
	};
	struct mipi_dsi_msg read_msg = {
		.type = MIPI_DSI_DCS_READ,
		.cmd = 0xda,
		.rx_buf = response,
		.rx_len = sizeof(response),
		.flags = MIPI_DSI_MSG_USE_LPM,
	};
	ssize_t ret;

	ret = mipi_dsi_transfer(config->mipi_dsi, config->channel, &max_msg);
	if (ret < 0) {
		return (int)ret;
	}
	k_msleep(10);
	ret = mipi_dsi_transfer(config->mipi_dsi, config->channel, &read_msg);
	if (ret < 0) {
		return (int)ret;
	}

	*panel_id = (uint16_t)response[0] | ((uint16_t)response[1] << 8);
	if ((*panel_id & 0xffU) == 0x01U || (*panel_id & 0xffU) == 0x03U ||
	    (*panel_id & 0xffU) == 0x11U || (*panel_id & 0xffU) == 0x17U ||
	    (*panel_id & 0xffU) == 0x57U) {
		*panel_id &= 0xffU;
	}
	return 0;
}

static int artinchip_pos_panel_init_sequence(const struct device *dev)
{
	const struct artinchip_pos_panel_config *config = dev->config;
	const struct pos_cmd *sequence = pos_id11_init;
	size_t count = ARRAY_SIZE(pos_id11_init);
	struct mipi_dsi_device mdev = {
		.data_lanes = config->data_lanes,
		.timings = config->timings,
		.pixfmt = config->pixel_format,
		.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			      MIPI_DSI_MODE_LPM,
	};
	bool run_common_tail = true;
	uint16_t panel_id = 0x11U;
	int ret = pos_read_panel_id(dev, &panel_id);

	if (ret != 0) {
		LOG_WRN("panel ID read failed (%d), falling back to ID 0x11", ret);
		panel_id = 0x11U;
	}

	switch (panel_id) {
	case 0x01:
	case 0x03:
		sequence = panel_id == 0x01U ? pos_id01_init : pos_id03_init;
		count = panel_id == 0x01U ? ARRAY_SIZE(pos_id01_init) :
					   ARRAY_SIZE(pos_id03_init);
		mdev.timings.hfp = 80;
		mdev.timings.hbp = 80;
		mdev.timings.hsync = 14;
		mdev.timings.vfp = 15;
		mdev.timings.vbp = 15;
		mdev.timings.vsync = 4;
		run_common_tail = false;
		break;
	case 0x17:
		sequence = pos_id17_init;
		count = ARRAY_SIZE(pos_id17_init);
		mdev.timings.hfp = 80; mdev.timings.hbp = 40; mdev.timings.hsync = 23;
		mdev.timings.vfp = 40; mdev.timings.vbp = 40; mdev.timings.vsync = 4;
		break;
	case 0x57:
		sequence = pos_id57_init;
		count = ARRAY_SIZE(pos_id57_init);
		mdev.timings.hfp = 80; mdev.timings.hbp = 80; mdev.timings.hsync = 14;
		mdev.timings.vfp = 15; mdev.timings.vbp = 15; mdev.timings.vsync = 4;
		break;
	case 0x1313:
		sequence = pos_id1313_init;
		count = ARRAY_SIZE(pos_id1313_init);
		mdev.timings.hfp = 80; mdev.timings.hbp = 40; mdev.timings.hsync = 23;
		mdev.timings.vfp = 40; mdev.timings.vbp = 40; mdev.timings.vsync = 4;
		break;
	case 0x11:
	default:
		mdev.timings.hfp = 80; mdev.timings.hbp = 84; mdev.timings.hsync = 23;
		mdev.timings.vfp = 19; mdev.timings.vbp = 20; mdev.timings.vsync = 4;
		break;
	}
	LOG_INF("POS panel ID 0x%04x", panel_id);

#if IS_ENABLED(CONFIG_DISPLAY_ARTINCHIP_DE) && DT_HAS_CHOSEN(zephyr_display)
	ret = artinchip_de_set_timings(DEVICE_DT_GET(DT_CHOSEN(zephyr_display)),
				       &mdev.timings);
	if (ret != 0) {
		LOG_ERR("failed to synchronize DE mode: %d", ret);
		return ret;
	}
#endif

	/* Update video timings only; stay in command mode for the init table. */
	ret = artinchip_dsi_update_timings(config->mipi_dsi, &mdev.timings);
	if (ret != 0) {
		return ret;
	}

	ret = pos_run_sequence(dev, sequence, count);
	if (ret != 0) {
		LOG_ERR("POS panel init sequence failed at ID 0x%04x: %d", panel_id, ret);
		return ret;
	}
	if (!run_common_tail) {
		return 0;
	}

	ret = artinchip_pos_panel_write_u8(dev, 0x36, 0x00);
	if (ret != 0) {
		return ret;
	}

	ret = artinchip_pos_panel_write(dev, MIPI_DCS_EXIT_SLEEP_MODE, NULL, 0U);
	if (ret != 0) {
		return ret;
	}
	k_msleep(120);

	ret = artinchip_pos_panel_write(dev, MIPI_DCS_SET_DISPLAY_ON, NULL, 0U);
	if (ret != 0) {
		return ret;
	}
	k_msleep(20);

	return 0;
}

static int artinchip_pos_panel_panel_reset(const struct device *dev)
{
	const struct artinchip_pos_panel_config *config = dev->config;
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

	k_msleep(10);
	ret = gpio_pin_set_dt(&config->reset, 1);
	if (ret != 0) {
		return ret;
	}

	k_msleep(20);
	ret = gpio_pin_set_dt(&config->reset, 0);
	if (ret != 0) {
		return ret;
	}

	k_msleep(120);
	return 0;
}

static int artinchip_pos_panel_attach_and_init(const struct device *dev)
{
	const struct artinchip_pos_panel_config *config = dev->config;
	int ret;

	ret = artinchip_dsi_set_videomode(config->mipi_dsi, false);
	if (ret != 0) {
		return ret;
	}

	ret = artinchip_pos_panel_init_sequence(dev);
	if (ret != 0) {
		return ret;
	}

	ret = artinchip_dsi_set_videomode(config->mipi_dsi, true);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

int artinchip_pos_panel_backlight_enable(const struct device *dev)
{
	const struct artinchip_pos_panel_config *config = dev->config;

	if (config->backlight.port == NULL) {
		return 0;
	}

	return gpio_pin_set_dt(&config->backlight, 1);
}

int artinchip_pos_panel_set_display(const struct device *dev, bool on)
{
	const struct artinchip_pos_panel_config *config = dev->config;
	int ret;
	int ret_video;

	/* DCS needs LP/command mode; video HS clock blocks PHY LP-stop waits. */
	ret = artinchip_dsi_set_videomode(config->mipi_dsi, false);
	if (ret != 0) {
		return ret;
	}

	ret = artinchip_pos_panel_write(
		dev, on ? MIPI_DCS_SET_DISPLAY_ON : MIPI_DCS_SET_DISPLAY_OFF, NULL, 0U);

	ret_video = artinchip_dsi_set_videomode(config->mipi_dsi, true);
	if (ret != 0) {
		return ret;
	}

	return ret_video;
}

int artinchip_pos_panel_attach_dsi(const struct device *dev)
{
	const struct artinchip_pos_panel_config *config = dev->config;
	struct mipi_dsi_device mdev = {
		.data_lanes = config->data_lanes,
		.timings = config->timings,
		.pixfmt = config->pixel_format,
		.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			      MIPI_DSI_MODE_LPM,
	};

	return mipi_dsi_attach(config->mipi_dsi, config->channel, &mdev);
}

int artinchip_pos_panel_bringup(const struct device *dev)
{
	int ret;

	/*
	 * PF0 is the panel/backlight enable (not PWM). Order matches LubanLite:
	 * enable PF0, settle, then toggle PD16 reset.
	 */
	ret = artinchip_pos_panel_backlight_enable(dev);
	if (ret != 0) {
		return ret;
	}
	k_msleep(150);

	ret = artinchip_pos_panel_panel_reset(dev);
	if (ret != 0) {
		return ret;
	}
	k_msleep(50);

	ret = artinchip_pos_panel_attach_and_init(dev);
	if (ret != 0) {
		LOG_ERR("panel bring-up failed: %d", ret);
	}

	return ret;
}

static int artinchip_pos_panel_blanking_on(const struct device *dev)
{
	const struct artinchip_pos_panel_config *config = dev->config;
	int ret;

	if (config->backlight.port != NULL) {
		ret = gpio_pin_set_dt(&config->backlight, 0);
		if (ret != 0) {
			return ret;
		}
	}

	return artinchip_pos_panel_write(dev, MIPI_DCS_SET_DISPLAY_OFF, NULL, 0U);
}

static int artinchip_pos_panel_blanking_off(const struct device *dev)
{
	const struct artinchip_pos_panel_config *config = dev->config;
	int ret;

	ret = artinchip_pos_panel_write(dev, MIPI_DCS_SET_DISPLAY_ON, NULL, 0U);
	if (ret != 0) {
		return ret;
	}

	if (config->backlight.port != NULL) {
		return gpio_pin_set_dt(&config->backlight, 1);
	}

	return 0;
}

static void artinchip_pos_panel_get_capabilities(const struct device *dev,
				       struct display_capabilities *capabilities)
{
	const struct artinchip_pos_panel_config *config = dev->config;

	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->x_resolution = config->width;
	capabilities->y_resolution = config->height;
	capabilities->supported_pixel_formats = PIXEL_FORMAT_RGB_888;
	capabilities->current_pixel_format = PIXEL_FORMAT_RGB_888;
	capabilities->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static int artinchip_pos_panel_set_pixel_format(const struct device *dev,
				      enum display_pixel_format pixel_format)
{
	ARG_UNUSED(dev);

	return (pixel_format == PIXEL_FORMAT_RGB_888) ? 0 : -ENOTSUP;
}

static DEVICE_API(display, artinchip_pos_panel_api) = {
	.blanking_on = artinchip_pos_panel_blanking_on,
	.blanking_off = artinchip_pos_panel_blanking_off,
	.get_capabilities = artinchip_pos_panel_get_capabilities,
	.set_pixel_format = artinchip_pos_panel_set_pixel_format,
};

static int artinchip_pos_panel_init(const struct device *dev)
{
	const struct artinchip_pos_panel_config *config = dev->config;
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

#if IS_ENABLED(CONFIG_ARTINCHIP_POS_PANEL_DEFER_PANEL_INIT)
	return 0;
#else
	ret = artinchip_pos_panel_backlight_enable(dev);
	if (ret != 0) {
		return ret;
	}
	k_msleep(150);

	ret = artinchip_pos_panel_panel_reset(dev);
	if (ret != 0) {
		return ret;
	}
	k_msleep(50);

	ret = artinchip_pos_panel_attach_and_init(dev);
	if (ret != 0) {
		LOG_ERR("panel init sequence failed: %d", ret);
		return ret;
	}

	return 0;
#endif
}

#define ARTINCHIP_POS_PANEL_DEFINE(inst)									\
	static const struct artinchip_pos_panel_config artinchip_pos_panel_config_##inst = {			\
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
	DEVICE_DT_INST_DEFINE(inst, artinchip_pos_panel_init, NULL, NULL,				\
			      &artinchip_pos_panel_config_##inst, POST_KERNEL,			\
			      CONFIG_DISPLAY_ARTINCHIP_POS_PANEL_INIT_PRIORITY, &artinchip_pos_panel_api);

DT_INST_FOREACH_STATUS_OKAY(ARTINCHIP_POS_PANEL_DEFINE)
