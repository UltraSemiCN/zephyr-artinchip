/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_mdio

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/mdio.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_MDIO_CTL          0x90U
#define AIC_MDIO_DATA         0x94U
#define AIC_MDIO_PHY_MASK     GENMASK(15, 11)
#define AIC_MDIO_REG_MASK     GENMASK(10, 6)
#define AIC_MDIO_CLK_MASK     GENMASK(5, 2)
#define AIC_MDIO_WRITE        BIT(1)
#define AIC_MDIO_BUSY         BIT(0)
#define AIC_MDIO_DATA_MASK    GENMASK(15, 0)
#define AIC_MDIO_TIMEOUT      K_MSEC(20)

struct mdio_artinchip_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_id;
	struct reset_dt_spec reset;
};

struct mdio_artinchip_data {
	struct k_mutex lock;
	uint32_t clock_range;
};

static uint32_t mdio_artinchip_clock_range(uint32_t rate)
{
	if (rate < 35000000U) {
		return 2U;
	}
	if (rate < 60000000U) {
		return 3U;
	}
	if (rate < 100000000U) {
		return 0U;
	}
	if (rate < 150000000U) {
		return 1U;
	}
	if (rate < 250000000U) {
		return 4U;
	}
	return 5U;
}

static int mdio_artinchip_transfer(const struct device *dev, uint8_t prtad, uint8_t regad,
			      uint16_t *value, bool write)
{
	const struct mdio_artinchip_config *config = dev->config;
	struct mdio_artinchip_data *data = dev->data;
	k_timepoint_t timeout;
	uint32_t control;

	if ((value == NULL) || (prtad > 31U) || (regad > 31U)) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (write) {
		sys_write32(*value, config->base + AIC_MDIO_DATA);
	}
	control = FIELD_PREP(AIC_MDIO_PHY_MASK, prtad) |
		  FIELD_PREP(AIC_MDIO_REG_MASK, regad) |
		  FIELD_PREP(AIC_MDIO_CLK_MASK, data->clock_range) |
		  (write ? AIC_MDIO_WRITE : 0U) | AIC_MDIO_BUSY;
	sys_write32(control, config->base + AIC_MDIO_CTL);

	timeout = sys_timepoint_calc(AIC_MDIO_TIMEOUT);
	while ((sys_read32(config->base + AIC_MDIO_CTL) & AIC_MDIO_BUSY) != 0U) {
		if (sys_timepoint_expired(timeout)) {
			k_mutex_unlock(&data->lock);
			return -ETIMEDOUT;
		}
		k_busy_wait(10);
	}

	if (!write) {
		*value = sys_read32(config->base + AIC_MDIO_DATA) & AIC_MDIO_DATA_MASK;
	}
	k_mutex_unlock(&data->lock);
	return 0;
}

static int mdio_artinchip_read(const struct device *dev, uint8_t prtad, uint8_t regad, uint16_t *value)
{
	return mdio_artinchip_transfer(dev, prtad, regad, value, false);
}

static int mdio_artinchip_write(const struct device *dev, uint8_t prtad, uint8_t regad, uint16_t value)
{
	return mdio_artinchip_transfer(dev, prtad, regad, &value, true);
}

static int mdio_artinchip_init(const struct device *dev)
{
	const struct mdio_artinchip_config *config = dev->config;
	struct mdio_artinchip_data *data = dev->data;
	uint32_t rate;
	int ret;

	if (!device_is_ready(config->clock_dev) || !device_is_ready(config->reset.dev)) {
		return -ENODEV;
	}
	ret = clock_control_on(config->clock_dev, config->clock_id);
	if (ret != 0) {
		return ret;
	}
	ret = reset_line_toggle_dt(&config->reset);
	if (ret != 0) {
		return ret;
	}
	ret = clock_control_get_rate(config->clock_dev, config->clock_id, &rate);
	if (ret != 0) {
		return ret;
	}
	data->clock_range = mdio_artinchip_clock_range(rate);
	k_mutex_init(&data->lock);
	return 0;
}

static DEVICE_API(mdio, mdio_artinchip_api) = {
	.read = mdio_artinchip_read,
	.write = mdio_artinchip_write,
};

#define MDIO_ARTINCHIP_INIT(inst)                                                                       \
	static struct mdio_artinchip_data mdio_artinchip_data_##inst;                                        \
	static const struct mdio_artinchip_config mdio_artinchip_config_##inst = {                            \
		.base = DT_REG_ADDR(DT_INST_PARENT(inst)),                                            \
		.clock_dev = DEVICE_DT_GET(DT_CLOCKS_CTLR(DT_INST_PARENT(inst))),                    \
		.clock_id = (clock_control_subsys_t)DT_CLOCKS_CELL(DT_INST_PARENT(inst), clkid),     \
		.reset = RESET_DT_SPEC_GET(DT_INST_PARENT(inst)),                                    \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, mdio_artinchip_init, NULL, &mdio_artinchip_data_##inst,                   \
			      &mdio_artinchip_config_##inst, POST_KERNEL, CONFIG_MDIO_INIT_PRIORITY,        \
			      &mdio_artinchip_api);

DT_INST_FOREACH_STATUS_OKAY(MDIO_ARTINCHIP_INIT)
