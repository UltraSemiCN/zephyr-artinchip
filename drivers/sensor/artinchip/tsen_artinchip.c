/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_tsen

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_TSEN_MCR               0x000U
#define AIC_TSEN_INTR              0x004U
#define AIC_TSEN_CH_CFG(ch)        (0x100U + ((ch) << 5))
#define AIC_TSEN_CH_FIL(ch)        (0x108U + ((ch) << 5))
#define AIC_TSEN_CH_DATA(ch)       (0x10CU + ((ch) << 5))
#define AIC_TSEN_CH_INT(ch)        (0x110U + ((ch) << 5))
#define AIC_TSEN_MCR_CTL_EN(ch)    (BIT(20) << (ch))
#define AIC_TSEN_MCR_SENSOR_EN(ch) (BIT(16) << (ch))
#define AIC_TSEN_MCR_ACQ_MASK      GENMASK(15, 8)
#define AIC_TSEN_MCR_EN            BIT(0)
#define AIC_TSEN_INTR_CH_EN(ch)    BIT(ch)
#define AIC_TSEN_CFG_ACQ_MASK      GENMASK(15, 8)
#define AIC_TSEN_CFG_ACQ_MAX       FIELD_PREP(AIC_TSEN_CFG_ACQ_MASK, 0xFFU)
#define AIC_TSEN_CFG_SINGLE        BIT(0)
#define AIC_TSEN_INT_DATA_RDY      BIT(16)
#define AIC_TSEN_FILTER_8_POINTS   3U
#define AIC_TSEN_ADC_MAX           4095U
#define AIC_TSEN_TIMEOUT_US        3000U

struct artinchip_tsen_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	uint8_t channel;
	int32_t slope;
	int32_t offset;
};

struct artinchip_tsen_data {
	int32_t temp_micro_c;
};

static uintptr_t artinchip_tsen_reg(const struct artinchip_tsen_config *config, uint32_t offset)
{
	return config->base + offset;
}

static void artinchip_tsen_channel_enable(const struct artinchip_tsen_config *config, bool enable)
{
	uint32_t mask = AIC_TSEN_MCR_CTL_EN(config->channel) |
			AIC_TSEN_MCR_SENSOR_EN(config->channel);
	uint32_t value = sys_read32(artinchip_tsen_reg(config, AIC_TSEN_MCR));

	value = enable ? (value | mask) : (value & ~mask);
	sys_write32(value, artinchip_tsen_reg(config, AIC_TSEN_MCR));
}

static int artinchip_tsen_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct artinchip_tsen_config *config = dev->config;
	struct artinchip_tsen_data *data = dev->data;
	uintptr_t cfg_reg = artinchip_tsen_reg(config, AIC_TSEN_CH_CFG(config->channel));
	uintptr_t int_reg = artinchip_tsen_reg(config, AIC_TSEN_CH_INT(config->channel));
	uint32_t raw;
	int ret;

	if ((chan != SENSOR_CHAN_ALL) && (chan != SENSOR_CHAN_DIE_TEMP)) {
		return -ENOTSUP;
	}

	sys_write32(AIC_TSEN_FILTER_8_POINTS,
		    artinchip_tsen_reg(config, AIC_TSEN_CH_FIL(config->channel)));
	sys_write32((sys_read32(cfg_reg) & ~AIC_TSEN_CFG_ACQ_MASK) |
			    AIC_TSEN_CFG_ACQ_MAX | AIC_TSEN_CFG_SINGLE,
		    cfg_reg);
	sys_write32(AIC_TSEN_INT_DATA_RDY, int_reg);
	artinchip_tsen_channel_enable(config, true);

	ret = WAIT_FOR((sys_read32(int_reg) & AIC_TSEN_INT_DATA_RDY) != 0U,
		       AIC_TSEN_TIMEOUT_US, k_busy_wait(1));
	raw = sys_read32(artinchip_tsen_reg(config, AIC_TSEN_CH_DATA(config->channel))) &
	      AIC_TSEN_ADC_MAX;
	sys_write32(sys_read32(int_reg), int_reg);
	artinchip_tsen_channel_enable(config, false);

	if (ret == 0) {
		return -ETIMEDOUT;
	}

	if ((raw == 0U) || (raw == AIC_TSEN_ADC_MAX)) {
		return -ENODATA;
	}

	/* Reference calibration yields deci-degrees Celsius. */
	data->temp_micro_c =
		(int32_t)(((int64_t)config->slope * raw + config->offset) / 1000LL) * 100000;

	return 0;
}

static int artinchip_tsen_channel_get(const struct device *dev, enum sensor_channel chan,
				 struct sensor_value *value)
{
	struct artinchip_tsen_data *data = dev->data;

	if (chan != SENSOR_CHAN_DIE_TEMP) {
		return -ENOTSUP;
	}

	value->val1 = data->temp_micro_c / 1000000;
	value->val2 = data->temp_micro_c % 1000000;

	return 0;
}

static int artinchip_tsen_init(const struct device *dev)
{
	const struct artinchip_tsen_config *config = dev->config;
	uint32_t value;
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

	value = sys_read32(artinchip_tsen_reg(config, AIC_TSEN_MCR));
	value |= AIC_TSEN_MCR_EN | AIC_TSEN_MCR_ACQ_MASK;
	sys_write32(value, artinchip_tsen_reg(config, AIC_TSEN_MCR));
	sys_write32(sys_read32(artinchip_tsen_reg(config, AIC_TSEN_INTR)) &
			    ~AIC_TSEN_INTR_CH_EN(config->channel),
		    artinchip_tsen_reg(config, AIC_TSEN_INTR));

	return 0;
}

static DEVICE_API(sensor, artinchip_tsen_api) = {
	.sample_fetch = artinchip_tsen_sample_fetch,
	.channel_get = artinchip_tsen_channel_get,
};

static const struct artinchip_tsen_config artinchip_tsen_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
	.channel = DT_INST_PROP(0, artinchip_channel),
	.slope = DT_INST_PROP(0, artinchip_slope),
	.offset = DT_INST_PROP(0, artinchip_offset),
};

static struct artinchip_tsen_data artinchip_tsen_data;

SENSOR_DEVICE_DT_INST_DEFINE(0, artinchip_tsen_init, NULL, &artinchip_tsen_data, &artinchip_tsen_config,
			     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &artinchip_tsen_api);
