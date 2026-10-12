/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT artinchip_d13x_ge
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/misc/artinchip_ge.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_GE_STATUS  0x004U
#define AIC_GE_VERSION 0x00CU
#define AIC_GE_BUSY    BIT(5)

struct artinchip_ge_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
};

uint32_t artinchip_ge_get_version(const struct device *dev)
{
	const struct artinchip_ge_config *cfg = dev->config;

	return sys_read32(cfg->base + AIC_GE_VERSION);
}

uint32_t artinchip_ge_get_status(const struct device *dev)
{
	const struct artinchip_ge_config *cfg = dev->config;

	return sys_read32(cfg->base + AIC_GE_STATUS);
}

bool artinchip_ge_is_busy(const struct device *dev)
{
	return (artinchip_ge_get_status(dev) & AIC_GE_BUSY) != 0U;
}

int artinchip_ge_reset(const struct device *dev)
{
	const struct artinchip_ge_config *cfg = dev->config;

	return reset_line_toggle_dt(&cfg->reset);
}

static int artinchip_ge_init(const struct device *dev)
{
	const struct artinchip_ge_config *cfg = dev->config;
	int ret;

	if (!device_is_ready(cfg->clock_dev) || !device_is_ready(cfg->reset.dev)) {
		return -ENODEV;
	}

	ret = clock_control_on(cfg->clock_dev, cfg->clock_subsys);
	return ret == 0 ? reset_line_toggle_dt(&cfg->reset) : ret;
}

static const struct artinchip_ge_config artinchip_ge_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
};

DEVICE_DT_INST_DEFINE(0, artinchip_ge_init, NULL, NULL, &artinchip_ge_config, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
