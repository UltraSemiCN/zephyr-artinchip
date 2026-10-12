/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_can

#include "can_sja1000.h"

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>

struct artinchip_can_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	void (*irq_config)(const struct device *dev);
};

static uint8_t artinchip_can_read_reg(const struct device *dev, uint8_t reg)
{
	const struct can_sja1000_config *config = dev->config;
	const struct artinchip_can_config *custom = config->custom;

	return (uint8_t)sys_read32(custom->base + ((uintptr_t)reg * sizeof(uint32_t)));
}

static void artinchip_can_write_reg(const struct device *dev, uint8_t reg, uint8_t value)
{
	const struct can_sja1000_config *config = dev->config;
	const struct artinchip_can_config *custom = config->custom;

	sys_write32(value, custom->base + ((uintptr_t)reg * sizeof(uint32_t)));
}

static int artinchip_can_get_core_clock(const struct device *dev, uint32_t *rate)
{
	const struct can_sja1000_config *config = dev->config;
	const struct artinchip_can_config *custom = config->custom;
	uint32_t bus_rate;
	int ret;

	ret = clock_control_get_rate(custom->clock_dev, custom->clock_subsys, &bus_rate);
	if (ret != 0) {
		return ret;
	}

	*rate = bus_rate / 2U;

	return 0;
}

static int artinchip_can_init(const struct device *dev)
{
	const struct can_sja1000_config *config = dev->config;
	const struct artinchip_can_config *custom = config->custom;
	int ret;

	ret = clock_control_on(custom->clock_dev, custom->clock_subsys);
	if (ret != 0) {
		return ret;
	}

	ret = reset_line_toggle_dt(&custom->reset);
	if (ret != 0) {
		return ret;
	}

	ret = can_sja1000_init(dev);
	if (ret != 0) {
		return ret;
	}

	custom->irq_config(dev);

	return 0;
}

static DEVICE_API(can, artinchip_can_api) = {
	.get_capabilities = can_sja1000_get_capabilities,
	.start = can_sja1000_start,
	.stop = can_sja1000_stop,
	.set_mode = can_sja1000_set_mode,
	.set_timing = can_sja1000_set_timing,
	.send = can_sja1000_send,
	.add_rx_filter = can_sja1000_add_rx_filter,
	.remove_rx_filter = can_sja1000_remove_rx_filter,
	.get_state = can_sja1000_get_state,
	.set_state_change_callback = can_sja1000_set_state_change_callback,
	.get_core_clock = artinchip_can_get_core_clock,
	.get_max_filters = can_sja1000_get_max_filters,
#ifdef CONFIG_CAN_MANUAL_RECOVERY_MODE
	.recover = can_sja1000_recover,
#endif
	.timing_min = CAN_SJA1000_TIMING_MIN_INITIALIZER,
	.timing_max = CAN_SJA1000_TIMING_MAX_INITIALIZER,
};

#define AIC_CAN_INIT(inst)                                                                      \
	static void artinchip_can_irq_config_##inst(const struct device *dev);                        \
	static const struct artinchip_can_config artinchip_can_config_##inst = {                           \
		.base = DT_INST_REG_ADDR(inst),                                                   \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                            \
		.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),          \
		.reset = RESET_DT_SPEC_INST_GET(inst),                                            \
		.irq_config = artinchip_can_irq_config_##inst,                                         \
	};                                                                                       \
	static const struct can_sja1000_config artinchip_can_sja_config_##inst =                       \
		CAN_SJA1000_DT_CONFIG_INST_GET(inst, &artinchip_can_config_##inst, artinchip_can_read_reg,  \
					       artinchip_can_write_reg, 0U, 0U, 10000U);             \
	static struct can_sja1000_data artinchip_can_data_##inst =                                     \
		CAN_SJA1000_DATA_INITIALIZER(NULL);                                                \
	CAN_DEVICE_DT_INST_DEFINE(inst, artinchip_can_init, NULL, &artinchip_can_data_##inst,                \
				  &artinchip_can_sja_config_##inst, POST_KERNEL, CONFIG_CAN_INIT_PRIORITY, \
				  &artinchip_can_api);                                                   \
	static void artinchip_can_irq_config_##inst(const struct device *dev)                           \
	{                                                                                         \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority), can_sja1000_isr,       \
			    DEVICE_DT_INST_GET(inst), 0);                                           \
		irq_enable(DT_INST_IRQN(inst));                                                     \
	}

DT_INST_FOREACH_STATUS_OKAY(AIC_CAN_INIT)
