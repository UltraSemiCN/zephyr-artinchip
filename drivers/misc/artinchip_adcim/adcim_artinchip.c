/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_adcim

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/misc/artinchip_adcim.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_ADCIM_CALCSR            0x004U
#define AIC_ADCIM_CAL_ENABLE        BIT(0)
#define AIC_ADCIM_CAL_VALUE         GENMASK(27, 16)
#define AIC_ADCIM_AUTO_CAL          0x08002F03U
#define AIC_ADCIM_CAL_SAMPLES       6U
#define AIC_ADCIM_CAL_TIMEOUT_US    1000U
#define AIC_ADCIM_STANDARD_VALUE    0x800
#define AIC_ADCIM_OFFSET_MISMATCH   0x28
#define AIC_ADCIM_MAX_VALUE         0xFFF

struct artinchip_adcim_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
};

struct artinchip_adcim_data {
	uint16_t calibration;
};

static int artinchip_adcim_auto_calibrate(const struct artinchip_adcim_config *config,
				     uint16_t *calibration)
{
	uint32_t samples[AIC_ADCIM_CAL_SAMPLES];
	uint32_t maximum = 0U;
	uint32_t minimum = UINT32_MAX;
	uint32_t total = 0U;

	for (uint32_t i = 0U; i < ARRAY_SIZE(samples); i++) {
		uint32_t timeout = AIC_ADCIM_CAL_TIMEOUT_US;

		sys_write32(AIC_ADCIM_AUTO_CAL, config->base + AIC_ADCIM_CALCSR);
		while ((sys_read32(config->base + AIC_ADCIM_CALCSR) &
			AIC_ADCIM_CAL_ENABLE) != 0U) {
			if (timeout-- == 0U) {
				return -ETIMEDOUT;
			}
			k_busy_wait(1U);
		}

		samples[i] = FIELD_GET(AIC_ADCIM_CAL_VALUE,
				       sys_read32(config->base + AIC_ADCIM_CALCSR));
		maximum = MAX(maximum, samples[i]);
		minimum = MIN(minimum, samples[i]);
		total += samples[i];
	}

	*calibration = (total - minimum - maximum) / (AIC_ADCIM_CAL_SAMPLES - 2U);
	return 0;
}

int artinchip_adcim_get_calibration(const struct device *dev, uint16_t *calibration)
{
	const struct artinchip_adcim_data *data;

	if ((dev == NULL) || (calibration == NULL) || !device_is_ready(dev)) {
		return -EINVAL;
	}

	data = dev->data;
	*calibration = data->calibration;
	return 0;
}

int artinchip_adcim_calibrate_value(const struct device *dev, uint16_t raw, uint16_t *calibrated)
{
	const struct artinchip_adcim_data *data;
	int32_t value;

	if ((dev == NULL) || (calibrated == NULL) || !device_is_ready(dev)) {
		return -EINVAL;
	}

	data = dev->data;
	value = raw + AIC_ADCIM_STANDARD_VALUE - data->calibration +
		AIC_ADCIM_OFFSET_MISMATCH;
	*calibrated = CLAMP(value, 0, AIC_ADCIM_MAX_VALUE);
	return 0;
}

static int artinchip_adcim_init(const struct device *dev)
{
	const struct artinchip_adcim_config *config = dev->config;
	struct artinchip_adcim_data *data = dev->data;
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

	return artinchip_adcim_auto_calibrate(config, &data->calibration);
}

static const struct artinchip_adcim_config artinchip_adcim_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
};

static struct artinchip_adcim_data artinchip_adcim_data;

DEVICE_DT_INST_DEFINE(0, artinchip_adcim_init, NULL, &artinchip_adcim_data, &artinchip_adcim_config,
		      POST_KERNEL, CONFIG_ARTINCHIP_ADCIM_INIT_PRIORITY, NULL);
