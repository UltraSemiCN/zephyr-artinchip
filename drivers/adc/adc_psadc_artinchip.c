/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_psadc

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/misc/artinchip_adcim.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_PSADC_MCR             0x000U
#define AIC_PSADC_TCR             0x004U
#define AIC_PSADC_NODE1           0x008U
#define AIC_PSADC_NODE2           0x00CU
#define AIC_PSADC_MSR             0x010U
#define AIC_PSADC_Q1FCR           0x020U
#define AIC_PSADC_Q1FDR           0x040U
#define AIC_PSADC_ENABLE          BIT(0)
#define AIC_PSADC_QUEUE_COMBINE   BIT(1)
#define AIC_PSADC_Q1_INT_ENABLE   BIT(18)
#define AIC_PSADC_Q1_SW_TRIGGER   BIT(22)
#define AIC_PSADC_Q1_INT          BIT(0)
#define AIC_PSADC_Q1_FIFO_ERROR   BIT(2)
#define AIC_PSADC_FIFO_FLUSH      BIT(0)
#define AIC_PSADC_FIFO_THRESHOLD  GENMASK(15, 8)
#define AIC_PSADC_FIFO_COUNT      GENMASK(28, 24)
#define AIC_PSADC_DATA            GENMASK(11, 0)
#define AIC_PSADC_NODE_WIDTH      4U
#define AIC_PSADC_QUEUE_LENGTH    8U
#define AIC_PSADC_MAX_CHANNELS    16U
#define AIC_PSADC_TIMEOUT_US      1000U

struct artinchip_psadc_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	const struct device *adcim;
	uint8_t channels;
};

struct artinchip_psadc_data {
	struct k_mutex lock;
};

static int artinchip_psadc_channel_setup(const struct device *dev,
				    const struct adc_channel_cfg *channel_cfg)
{
	const struct artinchip_psadc_config *config = dev->config;

	if ((channel_cfg->channel_id >= config->channels) || channel_cfg->differential ||
	    (channel_cfg->gain != ADC_GAIN_1) || (channel_cfg->reference != ADC_REF_INTERNAL) ||
	    (channel_cfg->acquisition_time != ADC_ACQ_TIME_DEFAULT)) {
		return -EINVAL;
	}

	return 0;
}

static uint32_t artinchip_psadc_build_nodes(uint32_t channels, uint8_t first, uint8_t count)
{
	uint32_t nodes = 0U;
	uint8_t node = 0U;

	for (uint8_t channel = 0U; channel < AIC_PSADC_MAX_CHANNELS; channel++) {
		if ((channels & BIT(channel)) == 0U) {
			continue;
		}
		if ((node >= first) && (node < (first + count))) {
			nodes |= channel << ((node - first) * AIC_PSADC_NODE_WIDTH);
		}
		node++;
	}

	return nodes;
}

static int artinchip_psadc_read(const struct device *dev, const struct adc_sequence *sequence)
{
	const struct artinchip_psadc_config *config = dev->config;
	struct artinchip_psadc_data *data = dev->data;
	uint16_t *buffer = sequence->buffer;
	uint32_t channels = sequence->channels;
	uint32_t count;
	uint32_t status;
	uint32_t timeout = AIC_PSADC_TIMEOUT_US;
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
	sys_write32(artinchip_psadc_build_nodes(channels, 0U, AIC_PSADC_QUEUE_LENGTH),
		    config->base + AIC_PSADC_NODE1);
	sys_write32(artinchip_psadc_build_nodes(channels, AIC_PSADC_QUEUE_LENGTH,
					  AIC_PSADC_QUEUE_LENGTH),
		    config->base + AIC_PSADC_NODE2);
	sys_write32(count - 1U, config->base + AIC_PSADC_TCR);
	sys_write32(FIELD_PREP(AIC_PSADC_FIFO_THRESHOLD, count) | AIC_PSADC_FIFO_FLUSH,
		    config->base + AIC_PSADC_Q1FCR);
	status = sys_read32(config->base + AIC_PSADC_MSR);
	sys_write32(status, config->base + AIC_PSADC_MSR);
	sys_write32(AIC_PSADC_ENABLE | AIC_PSADC_QUEUE_COMBINE | AIC_PSADC_Q1_INT_ENABLE |
			    AIC_PSADC_Q1_SW_TRIGGER,
		    config->base + AIC_PSADC_MCR);

	do {
		status = sys_read32(config->base + AIC_PSADC_MSR);
		if ((status & (AIC_PSADC_Q1_INT | AIC_PSADC_Q1_FIFO_ERROR)) != 0U) {
			break;
		}
		if (timeout-- == 0U) {
			ret = -ETIMEDOUT;
			goto out;
		}
		k_busy_wait(1U);
	} while (true);

	sys_write32(status, config->base + AIC_PSADC_MSR);
	if ((status & AIC_PSADC_Q1_FIFO_ERROR) != 0U) {
		ret = -EIO;
		goto out;
	}
	if (FIELD_GET(AIC_PSADC_FIFO_COUNT,
		      sys_read32(config->base + AIC_PSADC_Q1FCR)) < count) {
		ret = -EIO;
		goto out;
	}

	for (uint32_t i = 0U; i < count; i++) {
		uint16_t raw = sys_read32(config->base + AIC_PSADC_Q1FDR) & AIC_PSADC_DATA;

		ret = artinchip_adcim_calibrate_value(config->adcim, raw, &buffer[i]);
		if (ret != 0) {
			break;
		}
	}

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int artinchip_psadc_init(const struct device *dev)
{
	const struct artinchip_psadc_config *config = dev->config;
	struct artinchip_psadc_data *data = dev->data;
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

	sys_write32(AIC_PSADC_ENABLE | AIC_PSADC_QUEUE_COMBINE, config->base + AIC_PSADC_MCR);
	return 0;
}

static DEVICE_API(adc, artinchip_psadc_api) = {
	.channel_setup = artinchip_psadc_channel_setup,
	.read = artinchip_psadc_read,
	.ref_internal = DT_INST_PROP(0, vref_mv),
};

static const struct artinchip_psadc_config artinchip_psadc_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
	.adcim = DEVICE_DT_GET(DT_INST_PHANDLE(0, artinchip_adcim)),
	.channels = DT_INST_PROP(0, channels),
};

static struct artinchip_psadc_data artinchip_psadc_data;

DEVICE_DT_INST_DEFINE(0, artinchip_psadc_init, NULL, &artinchip_psadc_data, &artinchip_psadc_config,
		      POST_KERNEL, CONFIG_ADC_INIT_PRIORITY, &artinchip_psadc_api);
