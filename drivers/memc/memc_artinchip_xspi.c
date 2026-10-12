/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_xspi

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(memc_artinchip_xspi, CONFIG_MEMC_LOG_LEVEL);

#define AIC_XSPI_CTL          0x000U
#define AIC_XSPI_CTL_ENABLE   BIT(0)
#define AIC_XSPI_CTL_XIP      BIT(2)
#define AIC_XSPI_VERSION      0xFFCU

struct artinchip_xspi_config {
	uintptr_t base;
	uintptr_t memory_base;
	size_t memory_size;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
};

static int artinchip_xspi_init(const struct device *dev)
{
	const struct artinchip_xspi_config *config = dev->config;
	enum clock_control_status clock_status;
	uint32_t control;
	uint8_t reset_status;
	int ret;

	if (!device_is_ready(config->clock_dev) || !device_is_ready(config->reset.dev)) {
		return -ENODEV;
	}

	clock_status = clock_control_get_status(config->clock_dev, config->clock_subsys);
	if (clock_status != CLOCK_CONTROL_STATUS_ON) {
		LOG_ERR("bootloader left XSPI clock disabled");
		return -ENODEV;
	}

	ret = reset_status_dt(&config->reset, &reset_status);
	if (ret != 0) {
		return ret;
	}

	if (reset_status != 0U) {
		LOG_ERR("bootloader left XSPI in reset");
		return -ENODEV;
	}

	control = sys_read32(config->base + AIC_XSPI_CTL);
	if ((control & (AIC_XSPI_CTL_ENABLE | AIC_XSPI_CTL_XIP)) !=
	    (AIC_XSPI_CTL_ENABLE | AIC_XSPI_CTL_XIP)) {
		LOG_ERR("bootloader XSPI memory mapping is not enabled");
		return -ENODEV;
	}

	LOG_INF("preserving bootloader XSPI v%#x mapping at %#lx (%zu bytes)",
		sys_read32(config->base + AIC_XSPI_VERSION),
		(unsigned long)config->memory_base, config->memory_size);

	return 0;
}

#define AIC_XSPI_MEMORY_NODE DT_INST_PHANDLE(0, memory_region)

static const struct artinchip_xspi_config artinchip_xspi_config = {
	.base = DT_INST_REG_ADDR(0),
	.memory_base = DT_REG_ADDR(AIC_XSPI_MEMORY_NODE),
	.memory_size = DT_REG_SIZE(AIC_XSPI_MEMORY_NODE),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
};

DEVICE_DT_INST_DEFINE(0, artinchip_xspi_init, NULL, NULL, &artinchip_xspi_config, POST_KERNEL,
		      CONFIG_MEMC_INIT_PRIORITY, NULL);
