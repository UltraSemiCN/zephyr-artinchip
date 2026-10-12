/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT artinchip_d13x_mtop
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/misc/artinchip_mtop.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_MTOP_CTL       0x000U
#define AIC_MTOP_TIME      0x004U
#define AIC_MTOP_ENABLE    BIT(0)
#define AIC_MTOP_TRIGGER   BIT(29)
#define AIC_MTOP_GROUPS    4U
#define AIC_MTOP_PORTS     4U
#define AIC_MTOP_PORT_MASK 0x030F0F03U

struct artinchip_mtop_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
};

int artinchip_mtop_start(const struct device *dev, uint32_t period)
{
	const struct artinchip_mtop_config *config = dev->config;
	uint32_t control;

	if (period == 0U) {
		return -EINVAL;
	}
	sys_write32(period, config->base + AIC_MTOP_TIME);
	control = sys_read32(config->base + AIC_MTOP_CTL);
	sys_write32(control | AIC_MTOP_ENABLE | AIC_MTOP_TRIGGER,
		    config->base + AIC_MTOP_CTL);
	return 0;
}

void artinchip_mtop_stop(const struct device *dev)
{
	const struct artinchip_mtop_config *config = dev->config;
	uint32_t control = sys_read32(config->base + AIC_MTOP_CTL);

	sys_write32(control & ~AIC_MTOP_ENABLE, config->base + AIC_MTOP_CTL);
}

int artinchip_mtop_get_count(const struct device *dev, uint8_t group, uint8_t port,
			     struct artinchip_mtop_count *count)
{
	const struct artinchip_mtop_config *config = dev->config;
	uint32_t position = group * 8U + port;
	uint32_t offset;

	if ((count == NULL) || (group >= AIC_MTOP_GROUPS) || (port >= AIC_MTOP_PORTS) ||
	    ((AIC_MTOP_PORT_MASK & BIT(position)) == 0U)) {
		return -EINVAL;
	}
	offset = 0x100U + group * 0x100U + port * 0x20U;
	count->write = sys_read32(config->base + offset);
	count->read = sys_read32(config->base + offset + 4U);
	return 0;
}

static int artinchip_mtop_init(const struct device *dev)
{
	const struct artinchip_mtop_config *config = dev->config;
	int ret;

	if (!device_is_ready(config->clock_dev) || !device_is_ready(config->reset.dev)) {
		return -ENODEV;
	}
	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0) {
		return ret;
	}
	return reset_line_toggle_dt(&config->reset);
}

static const struct artinchip_mtop_config artinchip_mtop_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
};
DEVICE_DT_INST_DEFINE(0, artinchip_mtop_init, NULL, NULL, &artinchip_mtop_config, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
