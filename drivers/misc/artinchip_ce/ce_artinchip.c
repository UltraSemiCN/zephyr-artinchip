/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_ce

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/misc/artinchip_ce.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_CE_ICR       0x000U
#define AIC_CE_ISR       0x004U
#define AIC_CE_TCR       0x00CU
#define AIC_CE_TER       0x014U
#define AIC_CE_VERSION   0xFFCU
#define AIC_CE_UNITS     GENMASK(2, 0)
#define AIC_CE_TASK_START BIT(31)

struct artinchip_ce_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
};

uint32_t artinchip_ce_get_version(const struct device *dev)
{
	const struct artinchip_ce_config *config = dev->config;

	return sys_read32(config->base + AIC_CE_VERSION);
}

uint32_t artinchip_ce_get_status(const struct device *dev)
{
	const struct artinchip_ce_config *config = dev->config;

	return sys_read32(config->base + AIC_CE_ISR);
}

uint32_t artinchip_ce_get_errors(const struct device *dev)
{
	const struct artinchip_ce_config *config = dev->config;

	return sys_read32(config->base + AIC_CE_TER);
}

bool artinchip_ce_is_idle(const struct device *dev)
{
	const struct artinchip_ce_config *config = dev->config;

	return (sys_read32(config->base + AIC_CE_TCR) & AIC_CE_TASK_START) == 0U;
}

int artinchip_ce_reset(const struct device *dev)
{
	const struct artinchip_ce_config *config = dev->config;

	return reset_line_toggle_dt(&config->reset);
}

static int artinchip_ce_init(const struct device *dev)
{
	const struct artinchip_ce_config *config = dev->config;
	int ret;

	if (!device_is_ready(config->clock_dev) || !device_is_ready(config->reset.dev)) {
		return -ENODEV;
	}
	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0) {
		return ret;
	}
	ret = reset_line_toggle_dt(&config->reset);
	if (ret != 0) {
		return ret;
	}
	sys_write32(AIC_CE_UNITS, config->base + AIC_CE_ICR);
	sys_write32(AIC_CE_UNITS, config->base + AIC_CE_ISR);
	sys_write32(UINT32_MAX, config->base + AIC_CE_TER);
	return 0;
}

static const struct artinchip_ce_config artinchip_ce_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
};

DEVICE_DT_INST_DEFINE(0, artinchip_ce_init, NULL, NULL, &artinchip_ce_config, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
