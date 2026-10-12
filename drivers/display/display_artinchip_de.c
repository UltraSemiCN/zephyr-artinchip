/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_de

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/cache.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/artinchip_de.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>

#if defined(CONFIG_DISPLAY_ARTINCHIP_DE_SHELL)
#include <stdlib.h>
#include <zephyr/shell/shell.h>
#endif

LOG_MODULE_REGISTER(display_artinchip_de, CONFIG_DISPLAY_LOG_LEVEL);

#define DE_CTRL                0x000U
#define DE_CTRL_DITHER_EN      BIT(0)
#define DE_CTRL_GAMMA_EN       BIT(1)
#define DE_CTRL_CCM_EN         BIT(2)
#define DE_CONFIG_UPDATE       0x008U
#define DE_VERSION_ID          0x010U
#define UI_LAYER_CTRL          0x0a0U
#define UI_LAYER_SIZE          0x0a4U
#define UI_LAYER_RECT_CTRL     0x0b0U
#define UI_RECT_INPUT_SIZE(n)  (0x0c0U + (0x10U * (n)))
#define UI_RECT_OFFSET(n)      (0x0c4U + (0x10U * (n)))
#define UI_RECT_STRIDE(n)      (0x0c8U + (0x10U * (n)))
#define UI_RECT_ADDR(n)        (0x0ccU + (0x10U * (n)))
#define BLENDING_OUTPUT_SIZE   0x104U
#define UI_LAYER_OFFSET        0x10cU
#define SCALER0_CTRL           0x120U
#define WB_CTRL                0x170U
#define QOS_V_P0               0x880U
#define QOS_V_P1               0x884U
#define QOS_V_P2               0x888U
#define QOS_UI                 0x88cU
#define QOS_V_URGENT           0x890U
#define TIMING_CTRL            0x1d0U
#define TIMING_INIT            0x1d4U
#define TIMING_STATUS          0x1d8U
#define TIMING_LINE_SET        0x1dcU
#define TIMING_ACTIVE_SIZE     0x1e0U
#define TIMING_H_PORCH         0x1e4U
#define TIMING_V_PORCH         0x1e8U
#define TIMING_SYNC_PLUSE      0x1ecU
#define TIMING_POL_SET_REG     0x1f0U

#define ARTINCHIP_DE_BYTES_PER_PIXEL 2U
#define ARTINCHIP_DE_PIXEL_FORMAT    PIXEL_FORMAT_RGB_565
#define ARTINCHIP_DE_MPP_FMT_RGB565  0x0eU
#define ARTINCHIP_DE_PREFETCH_LINE   2U
#define ARTINCHIP_DE_MODE_CONTINUE   0U
#define ARTINCHIP_DE_ALPHA_GLOBAL    1U

#define DE_CONFIG_UPDATE_EN          BIT(0)
#define DE_SOFT_RESET_EN             BIT(16)
#define TIMING_CTRL_EN               BIT(0)
#define TIMING_DE_MODE_MASK          GENMASK(5, 4)
#define TIMING_DE_MODE(x)            FIELD_PREP(TIMING_DE_MODE_MASK, (x))
#define TIMING_LINE_PREFETCH_MASK    GENMASK(28, 16)
#define TIMING_LINE_PREFETCH(x)      FIELD_PREP(TIMING_LINE_PREFETCH_MASK, (x))
#define TIMING_ACTIVE_SIZE_SET(w, h) ((((h) & 0x1fffU) << 16) | ((w) & 0x1fffU))
#define TIMING_H_PORCH_SET(f, b)     ((((b) & 0x1fffU) << 16) | ((f) & 0x1fffU))
#define TIMING_V_PORCH_SET(f, b)     ((((b) & 0x1fffU) << 16) | ((f) & 0x1fffU))
#define TIMING_SYNC_SET(h, v)        ((((v) & 0x7ffU) << 16) | ((h) & 0x7ffU))
#define TIMING_POL_VALUE(h, v)       ((((v) & 0x1U) << 1) | ((h) & 0x1U))
#define UI_LAYER_SIZE_SET(w, h)      ((((h) & 0x1fffU) << 16) | ((w) & 0x1fffU))
#define UI_LAYER_OFFSET_SET(x, y)    ((((y) & 0x1fffU) << 16) | ((x) & 0x1fffU))
#define UI_RECT_INPUT_SIZE_SET(w, h) ((((h) & 0x1fffU) << 16) | ((w) & 0x1fffU))
#define UI_RECT_OFFSET_SET(x, y)     ((((y) & 0x1fffU) << 16) | ((x) & 0x1fffU))
#define UI_RECT_STRIDE_SET(x)        ((x) & 0x7fffU)
#define UI_LAYER_CTRL_FORMAT_MASK    GENMASK(14, 8)
#define UI_LAYER_CTRL_FORMAT(x)      FIELD_PREP(UI_LAYER_CTRL_FORMAT_MASK, (x))
#define UI_LAYER_CTRL_G_ALPHA_MASK   GENMASK(31, 24)
#define UI_LAYER_CTRL_G_ALPHA(x)     FIELD_PREP(UI_LAYER_CTRL_G_ALPHA_MASK, (x))
#define UI_LAYER_CTRL_ALPHA_MODE_MASK GENMASK(23, 22)
#define UI_LAYER_CTRL_ALPHA_MODE(x)  FIELD_PREP(UI_LAYER_CTRL_ALPHA_MODE_MASK, (x))
#define UI_LAYER_CTRL_BG_BLEND_EN    BIT(21)
#define UI_LAYER_CTRL_ALPHA_EN       BIT(2)
#define UI_LAYER_CTRL_EN             BIT(0)
#define UI_LAYER_RECT_CTRL_EN(x)     BIT(x)
#define BLENDING_OUTPUT_SIZE_SET(w, h) ((((h) & 0x1fffU) << 16) | ((w) & 0x1fffU))
#define DMAR_QOS_GREEN_MASK          GENMASK(31, 28)
#define DMAR_QOS_GREEN(x)            FIELD_PREP(DMAR_QOS_GREEN_MASK, (x))
#define DMAR_QOS_HIGH_MASK           GENMASK(25, 16)
#define DMAR_QOS_HIGH(x)             FIELD_PREP(DMAR_QOS_HIGH_MASK, (x))
#define DMAR_QOS_RED_MASK            GENMASK(15, 12)
#define DMAR_QOS_RED(x)              FIELD_PREP(DMAR_QOS_RED_MASK, (x))
#define DMAR_QOS_LOW_MASK            GENMASK(9, 0)
#define DMAR_QOS_LOW(x)              FIELD_PREP(DMAR_QOS_LOW_MASK, (x))
#define OUTSTANDING_MASK             GENMASK(20, 16)
#define OUTSTANDING(x)               FIELD_PREP(OUTSTANDING_MASK, (x))
#define DMAR_URGENT_EN               BIT(15)
#define DMAR_URGENT_TH_MASK          GENMASK(9, 0)
#define DMAR_URGENT_TH(x)            FIELD_PREP(DMAR_URGENT_TH_MASK, (x))
#define ARTINCHIP_DE_QOS_OUTSTANDING 0x1fU
#define ARTINCHIP_DE_QOS_URGENT_TH   0x40U

static const uint32_t artinchip_de_qos_values[] = {
	DMAR_QOS_GREEN(0x0dU) | DMAR_QOS_HIGH(0x20U) |
		DMAR_QOS_RED(0x0dU) | DMAR_QOS_LOW(0x10U),
	DMAR_QOS_GREEN(0x0dU) | DMAR_QOS_HIGH(0x20U) |
		DMAR_QOS_RED(0x0dU) | DMAR_QOS_LOW(0x10U),
	DMAR_QOS_GREEN(0x0dU) | DMAR_QOS_HIGH(0x20U) |
		DMAR_QOS_RED(0x0dU) | DMAR_QOS_LOW(0x10U),
	DMAR_QOS_GREEN(0x0dU) | DMAR_QOS_HIGH(0x60U) |
		DMAR_QOS_RED(0x0dU) | DMAR_QOS_LOW(0x40U),
};

struct artinchip_de_config {
	uintptr_t base;
	uint16_t width;
	uint16_t height;
	uint16_t hfront_porch;
	uint16_t hback_porch;
	uint16_t hsync_len;
	uint16_t vfront_porch;
	uint16_t vback_porch;
	uint16_t vsync_len;
	bool hsync_active;
	bool vsync_active;
	uintptr_t framebuffer;
	uint32_t framebuffer_size;
	bool manual_start;
	const struct device *clock_dev;
	clock_control_subsys_t clock_id;
	struct reset_dt_spec reset;
};

struct artinchip_de_data {
	enum display_pixel_format pixel_format;
	enum display_orientation orientation;
	bool blanked;
	bool initialized;
	bool regs_initialized;
};

static void artinchip_de_timing_enable(const struct device *dev, bool enable);

static uint32_t artinchip_de_read(const struct artinchip_de_config *config, uint32_t offset)
{
	return sys_read32(config->base + offset);
}

static void artinchip_de_write(const struct artinchip_de_config *config, uint32_t offset,
			       uint32_t value)
{
	sys_write32(value, config->base + offset);
}

static void artinchip_de_update_bits(const struct artinchip_de_config *config,
				     uint32_t offset, uint32_t mask, uint32_t value)
{
	uint32_t reg = artinchip_de_read(config, offset);

	reg &= ~mask;
	reg |= value & mask;
	artinchip_de_write(config, offset, reg);
}

static uint32_t artinchip_de_stride(const struct artinchip_de_config *config)
{
	return config->width * ARTINCHIP_DE_BYTES_PER_PIXEL;
}

static uint32_t artinchip_de_required_fb_size(const struct artinchip_de_config *config)
{
	return artinchip_de_stride(config) * config->height;
}

static void artinchip_de_configure_qos(const struct artinchip_de_config *config)
{
	artinchip_de_write(config, QOS_V_P0, artinchip_de_qos_values[0]);
	artinchip_de_write(config, QOS_V_P1, artinchip_de_qos_values[1]);
	artinchip_de_write(config, QOS_V_P2, artinchip_de_qos_values[2]);
	artinchip_de_write(config, QOS_UI, artinchip_de_qos_values[3]);
	artinchip_de_write(config, QOS_V_URGENT,
			   DMAR_URGENT_EN | OUTSTANDING(ARTINCHIP_DE_QOS_OUTSTANDING) |
				   DMAR_URGENT_TH(ARTINCHIP_DE_QOS_URGENT_TH));
}

static int artinchip_de_validate_area(const struct artinchip_de_config *config, uint16_t x,
				      uint16_t y, const struct display_buffer_descriptor *desc)
{
	uint32_t required;

	if ((desc == NULL) || (desc->width == 0U) || (desc->height == 0U)) {
		return -EINVAL;
	}

	if ((desc->pitch < desc->width) || ((uint32_t)x + desc->width > config->width) ||
	    ((uint32_t)y + desc->height > config->height)) {
		return -EINVAL;
	}

	required = ((desc->height - 1U) * desc->pitch + desc->width) *
		   ARTINCHIP_DE_BYTES_PER_PIXEL;
	if (desc->buf_size < required) {
		return -EINVAL;
	}

	return 0;
}

static void artinchip_de_fb_flush(const struct artinchip_de_config *config, size_t size)
{
	if (size == 0U) {
		return;
	}

	(void)sys_cache_data_flush_range((void *)config->framebuffer, size);
}

static int artinchip_de_blanking_on(const struct device *dev)
{
	artinchip_de_timing_enable(dev, false);
	return 0;
}

static int artinchip_de_blanking_off(const struct device *dev)
{
	const struct artinchip_de_data *data = dev->data;

	if (!data->regs_initialized) {
		return -EAGAIN;
	}

	artinchip_de_timing_enable(dev, true);
	return 0;
}

static int artinchip_de_write_fb(const struct device *dev, uint16_t x, uint16_t y,
				 const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct artinchip_de_config *config = dev->config;
	uint8_t *dst = (uint8_t *)config->framebuffer;
	const uint8_t *src = buf;
	uint32_t row_bytes;
	uintptr_t flush_start;
	size_t flush_size;

	if (buf == NULL) {
		return -EINVAL;
	}

	if (artinchip_de_validate_area(config, x, y, desc) != 0) {
		return -EINVAL;
	}

	row_bytes = desc->width * ARTINCHIP_DE_BYTES_PER_PIXEL;
	dst += ((uint32_t)y * artinchip_de_stride(config)) +
	       ((uint32_t)x * ARTINCHIP_DE_BYTES_PER_PIXEL);

	flush_start = (uintptr_t)dst;
	flush_size = (desc->height - 1U) * artinchip_de_stride(config) + row_bytes;

	for (uint16_t row = 0U; row < desc->height; row++) {
		memcpy(dst, src, row_bytes);
		dst += artinchip_de_stride(config);
		src += desc->pitch * ARTINCHIP_DE_BYTES_PER_PIXEL;
	}

#if defined(CONFIG_DCACHE)
	uintptr_t flush_end = ROUND_UP(flush_start + flush_size, CONFIG_DCACHE_LINE_SIZE);

	flush_start = ROUND_DOWN(flush_start, CONFIG_DCACHE_LINE_SIZE);
	flush_size = flush_end - flush_start;
#endif
	(void)sys_cache_data_flush_range((void *)flush_start, flush_size);
	return 0;
}

static int artinchip_de_read_fb(const struct device *dev, uint16_t x, uint16_t y,
				const struct display_buffer_descriptor *desc, void *buf)
{
	const struct artinchip_de_config *config = dev->config;
	const uint8_t *src = (const uint8_t *)config->framebuffer;
	uint8_t *dst = buf;
	uint32_t row_bytes;

	if (buf == NULL) {
		return -EINVAL;
	}

	if (artinchip_de_validate_area(config, x, y, desc) != 0) {
		return -EINVAL;
	}

	row_bytes = desc->width * ARTINCHIP_DE_BYTES_PER_PIXEL;
	src += ((uint32_t)y * artinchip_de_stride(config)) +
	       ((uint32_t)x * ARTINCHIP_DE_BYTES_PER_PIXEL);

	for (uint16_t row = 0U; row < desc->height; row++) {
		memcpy(dst, src, row_bytes);
		src += artinchip_de_stride(config);
		dst += desc->pitch * ARTINCHIP_DE_BYTES_PER_PIXEL;
	}

	return 0;
}

static int artinchip_de_clear(const struct device *dev)
{
	const struct artinchip_de_config *config = dev->config;

	memset((void *)config->framebuffer, 0, artinchip_de_required_fb_size(config));
	artinchip_de_fb_flush(config, artinchip_de_required_fb_size(config));
	return 0;
}

static int artinchip_de_hw_start(const struct device *dev);
static void artinchip_de_hw_init_regs(const struct device *dev);

static int artinchip_de_hw_start(const struct device *dev)
{
	const struct artinchip_de_config *config = dev->config;
	struct artinchip_de_data *data = dev->data;
	int ret;

	if (data->initialized) {
		return 0;
	}

	ret = clock_control_on(config->clock_dev, config->clock_id);
	if (ret != 0) {
		return ret;
	}

	ret = reset_line_deassert_dt(&config->reset);
	if (ret != 0) {
		(void)clock_control_off(config->clock_dev, config->clock_id);
		return ret;
	}

	data->initialized = true;

	return 0;
}

static void artinchip_de_configure_ui_alpha(const struct artinchip_de_config *config)
{
	/*
	 * RGB565 has no alpha channel: use global alpha 255 so the UI layer is
	 * fully opaque on the DPI/DSI path (pixel-alpha mode can leave bit0 as 0).
	 */
	artinchip_de_update_bits(config, UI_LAYER_CTRL, UI_LAYER_CTRL_BG_BLEND_EN, 0U);
	artinchip_de_update_bits(config, UI_LAYER_CTRL,
				 UI_LAYER_CTRL_G_ALPHA_MASK |
				 UI_LAYER_CTRL_ALPHA_MODE_MASK |
				 UI_LAYER_CTRL_ALPHA_EN,
				 UI_LAYER_CTRL_G_ALPHA(0xFFU) |
				 UI_LAYER_CTRL_ALPHA_MODE(ARTINCHIP_DE_ALPHA_GLOBAL) |
				 UI_LAYER_CTRL_ALPHA_EN);
}

static void artinchip_de_hw_init_regs(const struct device *dev)
{
	const struct artinchip_de_config *config = dev->config;
	struct artinchip_de_data *data = dev->data;

	artinchip_de_write(config, DE_CTRL, 0U);
	artinchip_de_write(config, TIMING_ACTIVE_SIZE,
			   TIMING_ACTIVE_SIZE_SET(config->width, config->height));
	artinchip_de_write(config, TIMING_H_PORCH,
			   TIMING_H_PORCH_SET(config->hfront_porch, config->hback_porch));
	artinchip_de_write(config, TIMING_V_PORCH,
			   TIMING_V_PORCH_SET(config->vfront_porch, config->vback_porch));
	artinchip_de_write(config, TIMING_SYNC_PLUSE,
			   TIMING_SYNC_SET(config->hsync_len, config->vsync_len));
	artinchip_de_write(config, TIMING_POL_SET_REG,
			   TIMING_POL_VALUE(config->hsync_active, config->vsync_active));
	artinchip_de_write(config, BLENDING_OUTPUT_SIZE,
			   BLENDING_OUTPUT_SIZE_SET(config->width, config->height));
	artinchip_de_write(config, UI_LAYER_SIZE,
			   UI_LAYER_SIZE_SET(config->width, config->height));
	artinchip_de_write(config, UI_LAYER_OFFSET, UI_LAYER_OFFSET_SET(0U, 0U));
	artinchip_de_write(config, UI_RECT_INPUT_SIZE(0),
			   UI_RECT_INPUT_SIZE_SET(config->width, config->height));
	artinchip_de_write(config, UI_RECT_OFFSET(0), UI_RECT_OFFSET_SET(0U, 0U));
	artinchip_de_write(config, UI_RECT_STRIDE(0), UI_RECT_STRIDE_SET(artinchip_de_stride(config)));
	artinchip_de_write(config, UI_RECT_ADDR(0), config->framebuffer);
	artinchip_de_write(config, UI_LAYER_RECT_CTRL, UI_LAYER_RECT_CTRL_EN(0));
	artinchip_de_update_bits(config, UI_LAYER_CTRL, UI_LAYER_CTRL_FORMAT_MASK,
				 UI_LAYER_CTRL_FORMAT(ARTINCHIP_DE_MPP_FMT_RGB565));
	artinchip_de_configure_ui_alpha(config);
	artinchip_de_update_bits(config, UI_LAYER_CTRL, UI_LAYER_CTRL_EN, UI_LAYER_CTRL_EN);
	artinchip_de_update_bits(config, SCALER0_CTRL, BIT(0), 0U);
	artinchip_de_update_bits(config, TIMING_LINE_SET, TIMING_LINE_PREFETCH_MASK,
				 TIMING_LINE_PREFETCH(ARTINCHIP_DE_PREFETCH_LINE));
	artinchip_de_update_bits(config, WB_CTRL, DE_SOFT_RESET_EN, DE_SOFT_RESET_EN);
	artinchip_de_configure_qos(config);
	artinchip_de_update_bits(config, TIMING_CTRL, TIMING_DE_MODE_MASK,
				 TIMING_DE_MODE(ARTINCHIP_DE_MODE_CONTINUE));
	artinchip_de_write(config, TIMING_STATUS, 0xffffffffU);
	artinchip_de_write(config, DE_CONFIG_UPDATE, DE_CONFIG_UPDATE_EN);
	data->regs_initialized = true;
}

static void artinchip_de_timing_enable(const struct device *dev, bool enable)
{
	const struct artinchip_de_config *config = dev->config;
	struct artinchip_de_data *data = dev->data;

	artinchip_de_update_bits(config, TIMING_CTRL, TIMING_CTRL_EN,
				 enable ? TIMING_CTRL_EN : 0U);
	data->blanked = !enable;
}

int artinchip_de_bringup(const struct device *dev, uint16_t fill_rgb565)
{
	const struct artinchip_de_config *config = dev->config;
	int ret;

	ret = artinchip_de_hw_start(dev);
	if (ret != 0) {
		return ret;
	}

	artinchip_de_hw_init_regs(dev);

	if (fill_rgb565 != 0U) {
		uint16_t *fb = (uint16_t *)config->framebuffer;
		uint32_t pixels = (uint32_t)config->width * config->height;

		for (uint32_t i = 0U; i < pixels; i++) {
			fb[i] = fill_rgb565;
		}
		artinchip_de_fb_flush(config, artinchip_de_required_fb_size(config));
	} else {
		ret = artinchip_de_clear(dev);
		if (ret != 0) {
			return ret;
		}
	}

	artinchip_de_timing_enable(dev, true);
	return 0;
}

int artinchip_de_test_pattern(const struct device *dev)
{
	const struct artinchip_de_config *config = dev->config;
	static const uint16_t bars[] = {
		0xffffU, /* white */
		0xffe0U, /* yellow */
		0x07ffU, /* cyan */
		0x07e0U, /* green */
		0xf81fU, /* magenta */
		0xf800U, /* red */
		0x001fU, /* blue */
		0x0000U, /* black */
	};
	uint16_t *fb = (uint16_t *)config->framebuffer;

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	for (uint32_t y = 0U; y < config->height; y++) {
		for (uint32_t x = 0U; x < config->width; x++) {
			uint32_t bar = ((uint64_t)x * ARRAY_SIZE(bars)) / config->width;

			fb[y * config->width + x] = bars[bar];
		}
	}
	artinchip_de_fb_flush(config, artinchip_de_required_fb_size(config));
	return 0;
}

int artinchip_de_set_timings(const struct device *dev,
			     const struct mipi_dsi_timings *timings)
{
	struct artinchip_de_config *config = (struct artinchip_de_config *)dev->config;
	const struct artinchip_de_data *data = dev->data;
	uint32_t required;

	if (timings == NULL || timings->hactive == 0U || timings->vactive == 0U) {
		return -EINVAL;
	}
	if (data->regs_initialized) {
		return -EBUSY;
	}
	required = timings->hactive * timings->vactive * ARTINCHIP_DE_BYTES_PER_PIXEL;
	if (required > config->framebuffer_size) {
		return -ENOMEM;
	}

	config->width = timings->hactive;
	config->height = timings->vactive;
	config->hfront_porch = timings->hfp;
	config->hback_porch = timings->hbp;
	config->hsync_len = timings->hsync;
	config->vfront_porch = timings->vfp;
	config->vback_porch = timings->vbp;
	config->vsync_len = timings->vsync;
	LOG_DBG("runtime mode %ux%u h=%u/%u/%u v=%u/%u/%u",
		config->width, config->height, config->hfront_porch,
		config->hback_porch, config->hsync_len, config->vfront_porch,
		config->vback_porch, config->vsync_len);
	return 0;
}

static void *artinchip_de_get_framebuffer(const struct device *dev)
{
	const struct artinchip_de_config *config = dev->config;

	return (void *)config->framebuffer;
}

static void artinchip_de_get_capabilities(const struct device *dev,
					  struct display_capabilities *capabilities)
{
	const struct artinchip_de_config *config = dev->config;
	const struct artinchip_de_data *data = dev->data;

	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->x_resolution = config->width;
	capabilities->y_resolution = config->height;
	capabilities->supported_pixel_formats = ARTINCHIP_DE_PIXEL_FORMAT;
	capabilities->current_pixel_format = data->pixel_format;
	capabilities->current_orientation = data->orientation;
}

static int artinchip_de_set_pixel_format(const struct device *dev,
					 enum display_pixel_format pixel_format)
{
	struct artinchip_de_data *data = dev->data;

	if (pixel_format != ARTINCHIP_DE_PIXEL_FORMAT) {
		return -ENOTSUP;
	}

	data->pixel_format = pixel_format;
	return 0;
}

static int artinchip_de_set_orientation(const struct device *dev,
					enum display_orientation orientation)
{
	struct artinchip_de_data *data = dev->data;

	if (orientation != DISPLAY_ORIENTATION_NORMAL) {
		return -ENOTSUP;
	}

	data->orientation = orientation;
	return 0;
}

static int artinchip_de_init(const struct device *dev)
{
	const struct artinchip_de_config *config = dev->config;
	struct artinchip_de_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->clock_dev) || !device_is_ready(config->reset.dev)) {
		return -ENODEV;
	}

	if ((config->framebuffer == 0U) ||
	    (config->framebuffer_size < artinchip_de_required_fb_size(config))) {
		return -EINVAL;
	}

	data->pixel_format = ARTINCHIP_DE_PIXEL_FORMAT;
	data->orientation = DISPLAY_ORIENTATION_NORMAL;
	data->blanked = true;

	if (config->manual_start) {
		LOG_DBG("DE hardware start deferred");
		return 0;
	}

	ret = artinchip_de_hw_start(dev);
	if (ret != 0) {
		return ret;
	}
	artinchip_de_hw_init_regs(dev);

	return artinchip_de_clear(dev);
}

static DEVICE_API(display, artinchip_de_api) = {
	.blanking_on = artinchip_de_blanking_on,
	.blanking_off = artinchip_de_blanking_off,
	.write = artinchip_de_write_fb,
	.read = artinchip_de_read_fb,
	.clear = artinchip_de_clear,
	.get_framebuffer = artinchip_de_get_framebuffer,
	.get_capabilities = artinchip_de_get_capabilities,
	.set_pixel_format = artinchip_de_set_pixel_format,
	.set_orientation = artinchip_de_set_orientation,
};

#if defined(CONFIG_DISPLAY_ARTINCHIP_DE_SHELL)
static const struct device *const artinchip_de_shell_dev = DEVICE_DT_GET(DT_DRV_INST(0));

static uint16_t artinchip_de_rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
	return (uint16_t)(((uint16_t)(r & 0xF8U) << 8) |
			  ((uint16_t)(g & 0xFCU) << 3) |
			  ((uint16_t)b >> 3));
}

static int artinchip_de_shell_parse_hex_byte(const struct shell *sh, const char *str,
					     uint8_t *channel)
{
	char *end;
	unsigned long value = strtoul(str, &end, 16);

	if ((end == str) || (*end != '\0') || (value > 0xffUL)) {
		shell_error(sh, "channel value %s out of range (00-FF)", str);
		return -EINVAL;
	}

	*channel = (uint8_t)value;
	return 0;
}

static int artinchip_de_shell_parse_hex_rgb888(const struct shell *sh, const char *str,
					       uint8_t *r, uint8_t *g, uint8_t *b)
{
	char *end;
	unsigned long value = strtoul(str, &end, 16);

	if ((end == str) || (*end != '\0') || (value > 0xffffffUL)) {
		shell_error(sh, "rgb888 value %s out of range (000000-FFFFFF)", str);
		return -EINVAL;
	}

	*r = (uint8_t)((value >> 16) & 0xffU);
	*g = (uint8_t)((value >> 8) & 0xffU);
	*b = (uint8_t)(value & 0xffU);
	return 0;
}

static int artinchip_de_shell_status(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_de_shell_dev;
	const struct artinchip_de_config *config = dev->config;
	const struct artinchip_de_data *data = dev->data;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(dev)) {
		shell_error(sh, "DE device is not ready");
		return -ENODEV;
	}

	shell_print(sh, "resolution: %ux%u RGB565", config->width, config->height);
	shell_print(sh, "framebuffer: 0x%08lx size: %u required: %u",
		    (unsigned long)config->framebuffer, config->framebuffer_size,
		    artinchip_de_required_fb_size(config));
	shell_print(sh, "manual-start: %s initialized: %s regs-initialized: %s blanked: %s",
		    config->manual_start ? "yes" : "no", data->initialized ? "yes" : "no",
		    data->regs_initialized ? "yes" : "no", data->blanked ? "yes" : "no");

	if (!data->regs_initialized) {
		shell_print(sh, "registers: unavailable until `aicde regs-init` succeeds");
		return 0;
	}

	shell_print(sh, "version=0x%08x update=0x%08x de_ctrl=0x%08x timing_ctrl=0x%08x timing_status=0x%08x",
		    artinchip_de_read(config, DE_VERSION_ID),
		    artinchip_de_read(config, DE_CONFIG_UPDATE),
		    artinchip_de_read(config, DE_CTRL),
		    artinchip_de_read(config, TIMING_CTRL),
		    artinchip_de_read(config, TIMING_STATUS));
	shell_print(sh, "active=0x%08x hporch=0x%08x vporch=0x%08x sync=0x%08x pol=0x%08x",
		    artinchip_de_read(config, TIMING_ACTIVE_SIZE),
		    artinchip_de_read(config, TIMING_H_PORCH),
		    artinchip_de_read(config, TIMING_V_PORCH),
		    artinchip_de_read(config, TIMING_SYNC_PLUSE),
		    artinchip_de_read(config, TIMING_POL_SET_REG));
	shell_print(sh, "ui_ctrl=0x%08x rect=0x%08x addr=0x%08x stride=0x%08x",
		    artinchip_de_read(config, UI_LAYER_CTRL),
		    artinchip_de_read(config, UI_LAYER_RECT_CTRL),
		    artinchip_de_read(config, UI_RECT_ADDR(0)),
		    artinchip_de_read(config, UI_RECT_STRIDE(0)));
	shell_print(sh, "qos: vp0=0x%08x vp1=0x%08x vp2=0x%08x ui=0x%08x urgent=0x%08x",
		    artinchip_de_read(config, QOS_V_P0),
		    artinchip_de_read(config, QOS_V_P1),
		    artinchip_de_read(config, QOS_V_P2),
		    artinchip_de_read(config, QOS_UI),
		    artinchip_de_read(config, QOS_V_URGENT));

	return 0;
}

static int artinchip_de_shell_init(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = artinchip_de_hw_start(artinchip_de_shell_dev);
	if (ret != 0) {
		shell_error(sh, "DE hardware start failed: %d", ret);
		return ret;
	}

	ret = artinchip_de_clear(artinchip_de_shell_dev);
	if (ret != 0) {
		shell_error(sh, "framebuffer clear failed: %d", ret);
		return ret;
	}

	shell_print(sh, "DE hardware started");
	return 0;
}

static int artinchip_de_shell_regs_init(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = artinchip_de_hw_start(artinchip_de_shell_dev);
	if (ret != 0) {
		shell_error(sh, "DE hardware start failed: %d", ret);
		return ret;
	}

	artinchip_de_hw_init_regs(artinchip_de_shell_dev);
	shell_print(sh, "DE timing and UI layer registers initialized");
	return 0;
}

static int artinchip_de_shell_timing_start(const struct shell *sh, size_t argc, char **argv)
{
	const struct artinchip_de_data *data = artinchip_de_shell_dev->data;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = artinchip_de_hw_start(artinchip_de_shell_dev);
	if (ret != 0) {
		shell_error(sh, "DE hardware start failed: %d", ret);
		return ret;
	}

	if (!data->regs_initialized) {
		artinchip_de_hw_init_regs(artinchip_de_shell_dev);
	}

	artinchip_de_timing_enable(artinchip_de_shell_dev, true);
	shell_print(sh, "DE timing started");
	return 0;
}

static int artinchip_de_shell_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (artinchip_de_clear(artinchip_de_shell_dev) != 0) {
		shell_error(sh, "clear failed");
		return -EIO;
	}

	shell_print(sh, "framebuffer cleared");
	return 0;
}

static int artinchip_de_shell_fill(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_de_shell_dev;
	const struct artinchip_de_config *config = dev->config;
	uint8_t r = 0xffU;
	uint8_t g = 0xffU;
	uint8_t b = 0xffU;
	uint16_t color;
	uint16_t *fb = (uint16_t *)config->framebuffer;
	uint32_t pixels = (uint32_t)config->width * config->height;
	int ret;

	if (argc == 2) {
		ret = artinchip_de_shell_parse_hex_rgb888(sh, argv[1], &r, &g, &b);
		if (ret != 0) {
			return ret;
		}
	} else if (argc == 4) {
		ret = artinchip_de_shell_parse_hex_byte(sh, argv[1], &r);
		if (ret != 0) {
			return ret;
		}

		ret = artinchip_de_shell_parse_hex_byte(sh, argv[2], &g);
		if (ret != 0) {
			return ret;
		}

		ret = artinchip_de_shell_parse_hex_byte(sh, argv[3], &b);
		if (ret != 0) {
			return ret;
		}
	} else if (argc != 1) {
		shell_error(sh, "usage: aicde fill [FFFFFF] | fill <RR> <GG> <BB>");
		return -EINVAL;
	}

	color = artinchip_de_rgb888_to_rgb565(r, g, b);

	for (uint32_t i = 0U; i < pixels; i++) {
		fb[i] = color;
	}
	artinchip_de_fb_flush(config, artinchip_de_required_fb_size(config));

	shell_print(sh, "framebuffer filled RGB888=%02X%02X%02X RGB565=0x%04X", r, g, b, color);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_aicde,
	SHELL_CMD(clear, NULL, "Clear the DE framebuffer", artinchip_de_shell_clear),
	SHELL_CMD_ARG(fill, NULL,
		      "Fill framebuffer: fill [FFFFFF] | fill <RR> <GG> <BB> (hex, -> RGB565)",
		      artinchip_de_shell_fill, 1, 3),
	SHELL_CMD(init, NULL, "Start DE clock/reset and clear the framebuffer",
		  artinchip_de_shell_init),
	SHELL_CMD(regs-init, NULL, "Initialize DE registers after clock/reset",
		  artinchip_de_shell_regs_init),
	SHELL_CMD(timing-start, NULL, "Initialize and enable DE timing output",
		  artinchip_de_shell_timing_start),
	SHELL_CMD(status, NULL, "Show DE framebuffer and register state", artinchip_de_shell_status),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(aicde, &sub_aicde, "ArtInChip display engine diagnostics", NULL);
#endif

#define ARTINCHIP_DE_INIT(inst)								\
	static struct artinchip_de_data artinchip_de_data_##inst;				\
	static struct artinchip_de_config artinchip_de_config_##inst = {			\
		.base = DT_INST_REG_ADDR(inst),							\
		.width = DT_INST_PROP(inst, width),					\
		.height = DT_INST_PROP(inst, height),					\
		.hfront_porch = DT_INST_PROP(inst, hfront_porch),			\
		.hback_porch = DT_INST_PROP(inst, hback_porch),				\
		.hsync_len = DT_INST_PROP(inst, hsync_len),				\
		.vfront_porch = DT_INST_PROP(inst, vfront_porch),			\
		.vback_porch = DT_INST_PROP(inst, vback_porch),				\
		.vsync_len = DT_INST_PROP(inst, vsync_len),				\
		.hsync_active = DT_INST_PROP(inst, hsync_active),			\
		.vsync_active = DT_INST_PROP(inst, vsync_active),			\
		.framebuffer = DT_INST_PROP(inst, framebuffer_address),			\
		.framebuffer_size = DT_INST_PROP(inst, framebuffer_size),		\
		.manual_start = DT_INST_PROP(inst, artinchip_manual_start),		\
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),			\
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),	\
		.reset = RESET_DT_SPEC_INST_GET(inst),					\
	};											\
	DEVICE_DT_INST_DEFINE(inst, artinchip_de_init, NULL,				\
			      &artinchip_de_data_##inst, &artinchip_de_config_##inst,	\
			      POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY, &artinchip_de_api);

DT_INST_FOREACH_STATUS_OKAY(ARTINCHIP_DE_INIT)
