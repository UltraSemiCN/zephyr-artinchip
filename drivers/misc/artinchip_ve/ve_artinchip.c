/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT artinchip_d13x_ve

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/misc/artinchip_ve.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>

#define AIC_VE_IRQ_ENABLE  0x00CU
#define AIC_VE_AVC_ENABLE  0x010U
#define AIC_VE_JPEG_ENABLE 0x014U
#define AIC_VE_PNG_ENABLE  0x018U
#define AIC_VE_PNG_STATUS  0x104U
#define AIC_VE_JPEG_STATUS 0x204U

struct artinchip_ve_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
};

int artinchip_ve_reset(const struct device *dev)
{
	const struct artinchip_ve_config *cfg = dev->config;

	return reset_line_toggle_dt(&cfg->reset);
}

void artinchip_ve_set_irq_enabled(const struct device *dev, bool enabled)
{
	const struct artinchip_ve_config *cfg = dev->config;

	sys_write32(enabled ? 1U : 0U, cfg->base + AIC_VE_IRQ_ENABLE);
}

uint32_t artinchip_ve_get_active_units(const struct device *dev)
{
	const struct artinchip_ve_config *cfg = dev->config;
	uint32_t active = 0U;

	if (sys_read32(cfg->base + AIC_VE_AVC_ENABLE) != 0U) {
		active |= ARTINCHIP_VE_UNIT_AVC;
	}
	if (sys_read32(cfg->base + AIC_VE_JPEG_ENABLE) != 0U) {
		active |= ARTINCHIP_VE_UNIT_JPEG;
	}
	if (sys_read32(cfg->base + AIC_VE_PNG_ENABLE) != 0U) {
		active |= ARTINCHIP_VE_UNIT_PNG;
	}

	return active;
}

uint32_t artinchip_ve_get_jpeg_status(const struct device *dev)
{
	const struct artinchip_ve_config *cfg = dev->config;

	return sys_read32(cfg->base + AIC_VE_JPEG_STATUS);
}

uint32_t artinchip_ve_get_png_status(const struct device *dev)
{
	const struct artinchip_ve_config *cfg = dev->config;

	return sys_read32(cfg->base + AIC_VE_PNG_STATUS);
}

static int artinchip_ve_init(const struct device *dev)
{
	const struct artinchip_ve_config *cfg = dev->config;
	int ret;

	if (!device_is_ready(cfg->clock_dev) || !device_is_ready(cfg->reset.dev)) {
		return -ENODEV;
	}

	ret = clock_control_on(cfg->clock_dev, cfg->clock_subsys);
	return ret == 0 ? reset_line_toggle_dt(&cfg->reset) : ret;
}

static const struct artinchip_ve_config artinchip_ve_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
};

DEVICE_DT_INST_DEFINE(0, artinchip_ve_init, NULL, NULL, &artinchip_ve_config, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
