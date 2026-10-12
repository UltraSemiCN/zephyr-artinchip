/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_gpai

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/misc/artinchip_adcim.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_GPAI_MCR                 0x000U
#define AIC_GPAI_MCR_ENABLE          BIT(0)
#define AIC_GPAI_MCR_CHANNEL(ch)     BIT(8U + (ch))
#define AIC_GPAI_CHANNEL_BASE(ch)    (0x100U + ((ch) * 0x40U))
#define AIC_GPAI_CHANNEL_CR(ch)      (AIC_GPAI_CHANNEL_BASE(ch) + 0x00U)
#define AIC_GPAI_CHANNEL_INT(ch)     (AIC_GPAI_CHANNEL_BASE(ch) + 0x04U)
#define AIC_GPAI_CHANNEL_FCR(ch)     (AIC_GPAI_CHANNEL_BASE(ch) + 0x20U)
#define AIC_GPAI_CHANNEL_DATA(ch)    (AIC_GPAI_CHANNEL_BASE(ch) + 0x24U)
#define AIC_GPAI_SINGLE_SAMPLE       BIT(0)
#define AIC_GPAI_ACQUISITION         GENMASK(15, 8)
#define AIC_GPAI_SAMPLE_COUNT        GENMASK(25, 24)
#define AIC_GPAI_EIGHT_SAMPLES       3U
#define AIC_GPAI_DEFAULT_ACQUISITION 0x2FU
#define AIC_GPAI_DATA_READY          BIT(16)
#define AIC_GPAI_FIFO_ERROR          BIT(17)
#define AIC_GPAI_FIFO_FLUSH          BIT(0)
#define AIC_GPAI_FIFO_COUNT          GENMASK(30, 24)
#define AIC_GPAI_SAMPLE_MASK         GENMASK(11, 0)
#define AIC_GPAI_TIMEOUT_US          1000U

struct artinchip_gpai_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	const struct device *adcim;
	const struct pinctrl_dev_config *pinctrl;
	uint8_t channels;
	uint16_t vref_mv;
};

struct artinchip_gpai_data {
	struct k_mutex lock;
};

static int artinchip_gpai_channel_setup(const struct device *dev,
				   const struct adc_channel_cfg *channel_cfg)
{
	const struct artinchip_gpai_config *config = dev->config;

	if ((channel_cfg->channel_id >= config->channels) || channel_cfg->differential ||
	    (channel_cfg->gain != ADC_GAIN_1) || (channel_cfg->reference != ADC_REF_INTERNAL) ||
	    (channel_cfg->acquisition_time != ADC_ACQ_TIME_DEFAULT)) {
		return -EINVAL;
	}

	return 0;
}

static int artinchip_gpai_sample_channel(const struct artinchip_gpai_config *config, uint8_t channel,
				    uint16_t *sample)
{
	uint32_t count;
	uint32_t total = 0U;
	uint32_t value;
	uint32_t timeout = AIC_GPAI_TIMEOUT_US;
	uint16_t raw;

	value = sys_read32(config->base + AIC_GPAI_MCR);
	sys_write32(value | AIC_GPAI_MCR_ENABLE | AIC_GPAI_MCR_CHANNEL(channel),
		    config->base + AIC_GPAI_MCR);
	sys_write32(AIC_GPAI_FIFO_FLUSH, config->base + AIC_GPAI_CHANNEL_FCR(channel));
	sys_write32(AIC_GPAI_DATA_READY | AIC_GPAI_FIFO_ERROR,
		    config->base + AIC_GPAI_CHANNEL_INT(channel));

	value = FIELD_PREP(AIC_GPAI_SAMPLE_COUNT, AIC_GPAI_EIGHT_SAMPLES) |
		FIELD_PREP(AIC_GPAI_ACQUISITION, AIC_GPAI_DEFAULT_ACQUISITION) |
		AIC_GPAI_SINGLE_SAMPLE;
	sys_write32(value, config->base + AIC_GPAI_CHANNEL_CR(channel));

	while ((sys_read32(config->base + AIC_GPAI_CHANNEL_INT(channel)) &
		(AIC_GPAI_DATA_READY | AIC_GPAI_FIFO_ERROR)) == 0U) {
		if (timeout-- == 0U) {
			return -ETIMEDOUT;
		}
		k_busy_wait(1U);
	}

	value = sys_read32(config->base + AIC_GPAI_CHANNEL_INT(channel));
	sys_write32(value, config->base + AIC_GPAI_CHANNEL_INT(channel));
	if ((value & AIC_GPAI_FIFO_ERROR) != 0U) {
		return -EIO;
	}

	count = FIELD_GET(AIC_GPAI_FIFO_COUNT,
			  sys_read32(config->base + AIC_GPAI_CHANNEL_FCR(channel)));
	if (count == 0U) {
		return -EIO;
	}

	for (uint32_t i = 0U; i < count; i++) {
		total += sys_read32(config->base + AIC_GPAI_CHANNEL_DATA(channel)) &
			 AIC_GPAI_SAMPLE_MASK;
	}
	raw = total / count;

	return artinchip_adcim_calibrate_value(config->adcim, raw, sample);
}

static int artinchip_gpai_read(const struct device *dev, const struct adc_sequence *sequence)
{
	const struct artinchip_gpai_config *config = dev->config;
	struct artinchip_gpai_data *data = dev->data;
	uint16_t *buffer = sequence->buffer;
	uint32_t channels = sequence->channels;
	uint32_t count;
	int ret = 0;

	if ((channels == 0U) || ((channels & ~BIT_MASK(config->channels)) != 0U) ||
	    (sequence->resolution != 12U)) {
		return -EINVAL;
	}
	if ((sequence->options != NULL) || (sequence->oversampling != 0U) || sequence->calibrate) {
		return -ENOTSUP;
	}

	count = POPCOUNT(channels);
	if ((buffer == NULL) || (sequence->buffer_size < count * sizeof(*buffer))) {
		return -ENOMEM;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	for (uint8_t channel = 0U; channel < config->channels; channel++) {
		if ((channels & BIT(channel)) == 0U) {
			continue;
		}

		ret = artinchip_gpai_sample_channel(config, channel, buffer++);
		if (ret != 0) {
			break;
		}
	}
	k_mutex_unlock(&data->lock);

	return ret;
}

static int artinchip_gpai_init(const struct device *dev)
{
	const struct artinchip_gpai_config *config = dev->config;
	struct artinchip_gpai_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->clock_dev) || !device_is_ready(config->reset.dev) ||
	    !device_is_ready(config->adcim)) {
		return -ENODEV;
	}

	k_mutex_init(&data->lock);
	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0) {
		return ret;
	}
	ret = reset_line_toggle_dt(&config->reset);
	if (ret != 0) {
		return ret;
	}

	if (config->pinctrl != NULL) {
		ret = pinctrl_apply_state(config->pinctrl, PINCTRL_STATE_DEFAULT);
		if (ret != 0) {
			return ret;
		}
	}

	sys_write32(AIC_GPAI_MCR_ENABLE, config->base + AIC_GPAI_MCR);
	return 0;
}

static DEVICE_API(adc, artinchip_gpai_api) = {
	.channel_setup = artinchip_gpai_channel_setup,
	.read = artinchip_gpai_read,
	.ref_internal = DT_INST_PROP(0, vref_mv),
};

#if DT_INST_NODE_HAS_PROP(0, pinctrl_0)
PINCTRL_DT_INST_DEFINE(0);
#define ARTINCHIP_GPAI_PINCTRL PINCTRL_DT_INST_DEV_CONFIG_GET(0)
#else
#define ARTINCHIP_GPAI_PINCTRL NULL
#endif

static const struct artinchip_gpai_config artinchip_gpai_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
	.adcim = DEVICE_DT_GET(DT_INST_PHANDLE(0, artinchip_adcim)),
	.pinctrl = ARTINCHIP_GPAI_PINCTRL,
	.channels = DT_INST_PROP(0, channels),
	.vref_mv = DT_INST_PROP(0, vref_mv),
};

static struct artinchip_gpai_data artinchip_gpai_data;

DEVICE_DT_INST_DEFINE(0, artinchip_gpai_init, NULL, &artinchip_gpai_data, &artinchip_gpai_config, POST_KERNEL,
		      CONFIG_ADC_INIT_PRIORITY, &artinchip_gpai_api);
