/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_rtp

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/input/input.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_RTP_MCR             0x000U
#define AIC_RTP_INTR            0x004U
#define AIC_RTP_PDEB            0x008U
#define AIC_RTP_PCTL            0x00CU
#define AIC_RTP_CHCFG           0x010U
#define AIC_RTP_FIL             0x018U
#define AIC_RTP_AMSC            0x01CU
#define AIC_RTP_FCR             0x020U
#define AIC_RTP_DATA            0x024U
#define AIC_RTP_DLY             0x028U
#define AIC_RTP_MCR_BYPASS      BIT(16)
#define AIC_RTP_MCR_PRES_EN     BIT(8)
#define AIC_RTP_MCR_AUTO2       FIELD_PREP(GENMASK(7, 4), 2U)
#define AIC_RTP_MCR_EN          BIT(0)
#define AIC_RTP_INTR_ERROR      (BIT(21) | BIT(20) | BIT(19))
#define AIC_RTP_INTR_DRDY       BIT(18)
#define AIC_RTP_INTR_RISE       BIT(17)
#define AIC_RTP_INTR_ENABLE     (BIT(5) | BIT(3) | BIT(2) | BIT(1))
#define AIC_RTP_CHCFG_ACQ       FIELD_PREP(GENMASK(15, 8), 0x2FU)
#define AIC_RTP_FILTER_4_8      3U
#define AIC_RTP_AMSC_PERIOD_EN  BIT(1)
#define AIC_RTP_FCR_COUNT_MASK  GENMASK(28, 24)
#define AIC_RTP_FCR_THRESHOLD   FIELD_PREP(GENMASK(12, 8), 4U)
#define AIC_RTP_FCR_ERROR_IE    (BIT(2) | BIT(1))
#define AIC_RTP_FCR_FLUSH       BIT(0)
#define AIC_RTP_DATA_MASK       GENMASK(11, 0)
#define AIC_RTP_AUTO2_WORDS     4U

struct artinchip_rtp_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	uint32_t sample_period_ms;
	uint32_t delay;
	void (*irq_config)(const struct device *dev);
};

struct artinchip_rtp_data {
	bool pressed;
};

static uintptr_t artinchip_rtp_reg(const struct artinchip_rtp_config *config, uint32_t offset)
{
	return config->base + offset;
}

static void artinchip_rtp_flush(const struct artinchip_rtp_config *config)
{
	sys_write32(sys_read32(artinchip_rtp_reg(config, AIC_RTP_FCR)) | AIC_RTP_FCR_FLUSH,
		    artinchip_rtp_reg(config, AIC_RTP_FCR));
}

static void artinchip_rtp_report_sample(const struct device *dev)
{
	const struct artinchip_rtp_config *config = dev->config;
	struct artinchip_rtp_data *data = dev->data;
	uint16_t sample[AIC_RTP_AUTO2_WORDS];

	for (uint32_t i = 0U; i < ARRAY_SIZE(sample); i++) {
		sample[i] = sys_read32(artinchip_rtp_reg(config, AIC_RTP_DATA)) & AIC_RTP_DATA_MASK;
	}

	input_report_abs(dev, INPUT_ABS_X, sample[0], false, K_NO_WAIT);
	input_report_abs(dev, INPUT_ABS_Y, sample[1], false, K_NO_WAIT);
	input_report_key(dev, INPUT_BTN_TOUCH, 1, true, K_NO_WAIT);
	data->pressed = true;
}

static void artinchip_rtp_isr(const struct device *dev)
{
	const struct artinchip_rtp_config *config = dev->config;
	struct artinchip_rtp_data *data = dev->data;
	uint32_t intr = sys_read32(artinchip_rtp_reg(config, AIC_RTP_INTR));
	uint32_t fcr = sys_read32(artinchip_rtp_reg(config, AIC_RTP_FCR));
	uint32_t count = FIELD_GET(AIC_RTP_FCR_COUNT_MASK, fcr);

	sys_write32(fcr, artinchip_rtp_reg(config, AIC_RTP_FCR));
	sys_write32(intr, artinchip_rtp_reg(config, AIC_RTP_INTR));

	if ((intr & AIC_RTP_INTR_ERROR) != 0U) {
		artinchip_rtp_flush(config);
		return;
	}

	if (((intr & AIC_RTP_INTR_DRDY) != 0U) && (count == AIC_RTP_AUTO2_WORDS)) {
		artinchip_rtp_report_sample(dev);
	} else if (count != 0U) {
		artinchip_rtp_flush(config);
	}

	if (((intr & AIC_RTP_INTR_RISE) != 0U) && data->pressed) {
		input_report_key(dev, INPUT_BTN_TOUCH, 0, true, K_NO_WAIT);
		data->pressed = false;
	}
}

static int artinchip_rtp_init(const struct device *dev)
{
	const struct artinchip_rtp_config *config = dev->config;
	uint32_t clock_rate;
	uint32_t period;
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

	ret = clock_control_get_rate(config->clock_dev, config->clock_subsys, &clock_rate);
	if (ret != 0) {
		return ret;
	}

	period = ((clock_rate / 1000U) * config->sample_period_ms) >> 12;
	artinchip_rtp_flush(config);
	sys_write32(0xFFFFFFFFU, artinchip_rtp_reg(config, AIC_RTP_PCTL));
	sys_write32(0xFF0FFF0FU, artinchip_rtp_reg(config, AIC_RTP_PDEB));
	sys_write32(config->delay, artinchip_rtp_reg(config, AIC_RTP_DLY));
	sys_write32(AIC_RTP_CHCFG_ACQ, artinchip_rtp_reg(config, AIC_RTP_CHCFG));
	sys_write32(AIC_RTP_FILTER_4_8, artinchip_rtp_reg(config, AIC_RTP_FIL));
	sys_write32((period << 12) | AIC_RTP_AMSC_PERIOD_EN,
		    artinchip_rtp_reg(config, AIC_RTP_AMSC));
	sys_write32(AIC_RTP_FCR_THRESHOLD | AIC_RTP_FCR_ERROR_IE,
		    artinchip_rtp_reg(config, AIC_RTP_FCR));
	sys_write32(AIC_RTP_INTR_ENABLE, artinchip_rtp_reg(config, AIC_RTP_INTR));
	sys_write32(AIC_RTP_MCR_BYPASS | AIC_RTP_MCR_PRES_EN | AIC_RTP_MCR_AUTO2 |
			    AIC_RTP_MCR_EN,
		    artinchip_rtp_reg(config, AIC_RTP_MCR));

	config->irq_config(dev);
	return 0;
}

static void artinchip_rtp_irq_config(const struct device *dev)
{
	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), artinchip_rtp_isr, DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));
}

static const struct artinchip_rtp_config artinchip_rtp_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
	.sample_period_ms = DT_INST_PROP(0, sample_period_ms),
	.delay = DT_INST_PROP(0, artinchip_delay),
	.irq_config = artinchip_rtp_irq_config,
};

static struct artinchip_rtp_data artinchip_rtp_data;

DEVICE_DT_INST_DEFINE(0, artinchip_rtp_init, NULL, &artinchip_rtp_data, &artinchip_rtp_config, POST_KERNEL,
		      CONFIG_INPUT_INIT_PRIORITY, NULL);
