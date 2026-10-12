/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_i2s

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define I2S_CTL                    0x00U
#define I2S_FMT0                   0x04U
#define I2S_FCTL                   0x14U
#define I2S_CLKD                   0x24U
#define I2S_CHCFG                  0x30U
#define I2S_TXCHSEL                0x34U
#define I2S_RXCHSEL                0x64U

#define I2S_CTL_GEN                BIT(0)
#define I2S_CTL_RXEN               BIT(1)
#define I2S_CTL_TXEN               BIT(2)
#define I2S_CTL_LOOP               BIT(3)
#define I2S_CTL_MODE_MASK          GENMASK(5, 4)
#define I2S_CTL_MODE_LEFT          FIELD_PREP(I2S_CTL_MODE_MASK, 1U)
#define I2S_CTL_MODE_RIGHT         FIELD_PREP(I2S_CTL_MODE_MASK, 2U)
#define I2S_CTL_DOUT_EN            BIT(8)
#define I2S_CTL_LRCK_CONTROLLER    BIT(17)
#define I2S_CTL_BCLK_CONTROLLER    BIT(18)
#define I2S_FMT0_SLOT_WIDTH_MASK   GENMASK(2, 0)
#define I2S_FMT0_SAMPLE_WIDTH_MASK GENMASK(6, 4)
#define I2S_FMT0_BCLK_POLARITY     BIT(7)
#define I2S_FMT0_LRCK_PERIOD_MASK  GENMASK(17, 8)
#define I2S_FMT0_LRCK_POLARITY     BIT(19)
#define I2S_FCTL_FLUSH_RX          BIT(24)
#define I2S_FCTL_FLUSH_TX          BIT(25)
#define I2S_CLKD_MCLK_OUT_EN       BIT(8)
#define I2S_CHCFG_TX_MASK          GENMASK(3, 0)
#define I2S_CHCFG_RX_MASK          GENMASK(7, 4)
#define I2S_CHSEL_EN_MASK          GENMASK(15, 0)
#define I2S_CHSEL_COUNT_MASK       GENMASK(19, 16)

struct i2s_artinchip_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_id;
	struct reset_dt_spec reset;
};

struct i2s_artinchip_data {
	struct i2s_config tx;
	struct i2s_config rx;
	bool tx_configured;
	bool rx_configured;
};

static void i2s_artinchip_update(uintptr_t addr, uint32_t mask, uint32_t value)
{
	sys_write32((sys_read32(addr) & ~mask) | value, addr);
}

static int i2s_artinchip_configure(const struct device *dev, enum i2s_dir dir,
			      const struct i2s_config *cfg)
{
	const struct i2s_artinchip_config *config = dev->config;
	struct i2s_artinchip_data *data = dev->data;
	uint32_t width_code;
	uint32_t ctl = 0U;
	uint32_t fmt0;
	uint32_t channels;

	if ((cfg == NULL) || (dir == I2S_DIR_BOTH) || (cfg->channels == 0U) ||
	    (cfg->channels > 16U) || (cfg->word_size < 8U) || (cfg->word_size > 32U) ||
	    ((cfg->word_size % 4U) != 0U)) {
		return -EINVAL;
	}

	if (cfg->frame_clk_freq == 0U) {
		if (dir == I2S_DIR_TX) {
			data->tx_configured = false;
		} else {
			data->rx_configured = false;
		}
		return 0;
	}

	switch (cfg->format & I2S_FMT_DATA_FORMAT_MASK) {
	case I2S_FMT_DATA_FORMAT_I2S:
	case I2S_FMT_DATA_FORMAT_PCM_SHORT:
	case I2S_FMT_DATA_FORMAT_PCM_LONG:
		break;
	case I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED:
		ctl |= I2S_CTL_MODE_LEFT;
		break;
	case I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED:
		ctl |= I2S_CTL_MODE_RIGHT;
		break;
	default:
		return -ENOTSUP;
	}

	if ((cfg->options & I2S_OPT_BIT_CLK_TARGET) == 0U) {
		ctl |= I2S_CTL_BCLK_CONTROLLER;
	}
	if ((cfg->options & I2S_OPT_FRAME_CLK_TARGET) == 0U) {
		ctl |= I2S_CTL_LRCK_CONTROLLER;
	}
	if ((cfg->options & I2S_OPT_LOOPBACK) != 0U) {
		ctl |= I2S_CTL_LOOP;
	}

	width_code = ((cfg->word_size - 8U) / 4U) + 1U;
	fmt0 = FIELD_PREP(I2S_FMT0_SLOT_WIDTH_MASK, width_code) |
	       FIELD_PREP(I2S_FMT0_SAMPLE_WIDTH_MASK, width_code) |
	       FIELD_PREP(I2S_FMT0_LRCK_PERIOD_MASK, cfg->word_size - 1U);
	if ((cfg->format & I2S_FMT_BIT_CLK_INV) != 0U) {
		fmt0 |= I2S_FMT0_BCLK_POLARITY;
	}
	if ((cfg->format & I2S_FMT_FRAME_CLK_INV) != 0U) {
		fmt0 |= I2S_FMT0_LRCK_POLARITY;
	}

	channels = cfg->channels - 1U;
	sys_write32(fmt0, config->base + I2S_FMT0);
	i2s_artinchip_update(config->base + I2S_CTL,
			I2S_CTL_MODE_MASK | I2S_CTL_LOOP | I2S_CTL_LRCK_CONTROLLER |
				I2S_CTL_BCLK_CONTROLLER,
			ctl);
	if (dir == I2S_DIR_TX) {
		i2s_artinchip_update(config->base + I2S_CHCFG, I2S_CHCFG_TX_MASK,
				FIELD_PREP(I2S_CHCFG_TX_MASK, channels));
		sys_write32(FIELD_PREP(I2S_CHSEL_COUNT_MASK, channels) |
				    FIELD_PREP(I2S_CHSEL_EN_MASK, BIT(cfg->channels) - 1U),
			    config->base + I2S_TXCHSEL);
		data->tx = *cfg;
		data->tx_configured = true;
	} else {
		i2s_artinchip_update(config->base + I2S_CHCFG, I2S_CHCFG_RX_MASK,
				FIELD_PREP(I2S_CHCFG_RX_MASK, channels));
		sys_write32(FIELD_PREP(I2S_CHSEL_COUNT_MASK, channels),
			    config->base + I2S_RXCHSEL);
		data->rx = *cfg;
		data->rx_configured = true;
	}

	sys_write32(I2S_CLKD_MCLK_OUT_EN, config->base + I2S_CLKD);
	return 0;
}

static const struct i2s_config *i2s_artinchip_config_get(const struct device *dev, enum i2s_dir dir)
{
	struct i2s_artinchip_data *data = dev->data;

	if (dir == I2S_DIR_TX) {
		return data->tx_configured ? &data->tx : NULL;
	}
	if (dir == I2S_DIR_RX) {
		return data->rx_configured ? &data->rx : NULL;
	}
	return NULL;
}

static int i2s_artinchip_read(const struct device *dev, void **mem_block, size_t *size)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(mem_block);
	ARG_UNUSED(size);
	return -ENOTSUP;
}

static int i2s_artinchip_write(const struct device *dev, void *mem_block, size_t size)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(mem_block);
	ARG_UNUSED(size);
	return -ENOTSUP;
}

static int i2s_artinchip_trigger(const struct device *dev, enum i2s_dir dir, enum i2s_trigger_cmd cmd)
{
	const struct i2s_artinchip_config *config = dev->config;
	struct i2s_artinchip_data *data = dev->data;
	uint32_t mask;

	if ((dir == I2S_DIR_BOTH) || ((dir == I2S_DIR_TX) && !data->tx_configured) ||
	    ((dir == I2S_DIR_RX) && !data->rx_configured)) {
		return -EINVAL;
	}

	mask = dir == I2S_DIR_TX ? I2S_CTL_TXEN | I2S_CTL_DOUT_EN : I2S_CTL_RXEN;
	switch (cmd) {
	case I2S_TRIGGER_START:
		i2s_artinchip_update(config->base + I2S_CTL, mask | I2S_CTL_GEN, mask | I2S_CTL_GEN);
		return 0;
	case I2S_TRIGGER_STOP:
	case I2S_TRIGGER_DRAIN:
	case I2S_TRIGGER_DROP:
	case I2S_TRIGGER_PREPARE:
		i2s_artinchip_update(config->base + I2S_CTL, mask, 0U);
		sys_write32(dir == I2S_DIR_TX ? I2S_FCTL_FLUSH_TX : I2S_FCTL_FLUSH_RX,
			    config->base + I2S_FCTL);
		return 0;
	default:
		return -EINVAL;
	}
}

static int i2s_artinchip_init(const struct device *dev)
{
	const struct i2s_artinchip_config *config = dev->config;
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
	sys_write32(I2S_FCTL_FLUSH_TX | I2S_FCTL_FLUSH_RX, config->base + I2S_FCTL);
	return 0;
}

static DEVICE_API(i2s, i2s_artinchip_api) = {
	.configure = i2s_artinchip_configure,
	.config_get = i2s_artinchip_config_get,
	.read = i2s_artinchip_read,
	.write = i2s_artinchip_write,
	.trigger = i2s_artinchip_trigger,
};

#define I2S_ARTINCHIP_INIT(inst)                                                                        \
	static struct i2s_artinchip_data i2s_artinchip_data_##inst;                                          \
	static const struct i2s_artinchip_config i2s_artinchip_config_##inst = {                              \
		.base = DT_INST_REG_ADDR(inst),                                                       \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                                \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),                 \
		.reset = RESET_DT_SPEC_INST_GET(inst),                                                \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, i2s_artinchip_init, NULL, &i2s_artinchip_data_##inst,                     \
			      &i2s_artinchip_config_##inst, POST_KERNEL, CONFIG_I2S_INIT_PRIORITY,          \
			      &i2s_artinchip_api);

DT_INST_FOREACH_STATUS_OKAY(I2S_ARTINCHIP_INIT)
