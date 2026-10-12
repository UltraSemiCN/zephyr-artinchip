/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_syscfg

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/syscon.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/sys_io.h>

#define AIC_SYSCFG_VERSION 0xFFCU

struct artinchip_syscfg_config {
	uintptr_t base;
	size_t size;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
};

struct artinchip_syscfg_data {
	struct k_spinlock lock;
};

static int artinchip_syscfg_validate(const struct artinchip_syscfg_config *config, uint32_t reg)
{
	return ((reg & 0x3U) == 0U) && (reg < config->size) ? 0 : -EINVAL;
}

static int artinchip_syscfg_read(const struct device *dev, uint32_t reg, uint32_t *value)
{
	const struct artinchip_syscfg_config *config = dev->config;

	if ((value == NULL) || (artinchip_syscfg_validate(config, reg) != 0)) {
		return -EINVAL;
	}

	*value = sys_read32(config->base + reg);

	return 0;
}

static int artinchip_syscfg_write(const struct device *dev, uint32_t reg, uint32_t value)
{
	const struct artinchip_syscfg_config *config = dev->config;

	if (artinchip_syscfg_validate(config, reg) != 0) {
		return -EINVAL;
	}

	sys_write32(value, config->base + reg);

	return 0;
}

static int artinchip_syscfg_update_bits(const struct device *dev, uint32_t reg, uint32_t mask,
				   uint32_t value)
{
	const struct artinchip_syscfg_config *config = dev->config;
	struct artinchip_syscfg_data *data = dev->data;
	uint32_t current;

	if (artinchip_syscfg_validate(config, reg) != 0) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		current = sys_read32(config->base + reg);
		sys_write32((current & ~mask) | (value & mask), config->base + reg);
	}

	return 0;
}

static int artinchip_syscfg_get_base(const struct device *dev, uintptr_t *base)
{
	const struct artinchip_syscfg_config *config = dev->config;

	*base = config->base;

	return 0;
}

static int artinchip_syscfg_get_size(const struct device *dev, size_t *size)
{
	const struct artinchip_syscfg_config *config = dev->config;

	*size = config->size;

	return 0;
}

static int artinchip_syscfg_init(const struct device *dev)
{
	const struct artinchip_syscfg_config *config = dev->config;
	int ret;

	if (!device_is_ready(config->clock_dev) || !device_is_ready(config->reset.dev)) {
		return -ENODEV;
	}

	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0) {
		return ret;
	}

	ret = reset_line_deassert_dt(&config->reset);
	if (ret != 0) {
		return ret;
	}

	if (sys_read32(config->base + AIC_SYSCFG_VERSION) == 0U) {
		return -ENODEV;
	}

	return 0;
}

static DEVICE_API(syscon, artinchip_syscfg_api) = {
	.read = artinchip_syscfg_read,
	.write = artinchip_syscfg_write,
	.update_bits = artinchip_syscfg_update_bits,
	.get_base = artinchip_syscfg_get_base,
	.get_size = artinchip_syscfg_get_size,
};

static const struct artinchip_syscfg_config artinchip_syscfg_config = {
	.base = DT_INST_REG_ADDR(0),
	.size = DT_INST_REG_SIZE(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
};

static struct artinchip_syscfg_data artinchip_syscfg_data;

DEVICE_DT_INST_DEFINE(0, artinchip_syscfg_init, NULL, &artinchip_syscfg_data, &artinchip_syscfg_config,
		      PRE_KERNEL_1, CONFIG_SYSCON_INIT_PRIORITY, &artinchip_syscfg_api);
