/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_qspi

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(spi_artinchip, CONFIG_SPI_LOG_LEVEL);

#include "spi_context.h"

#define AIC_QSPI_CFG             0x04U
#define AIC_QSPI_TCFG            0x08U
#define AIC_QSPI_ICR             0x10U
#define AIC_QSPI_ISTS            0x14U
#define AIC_QSPI_FCTL            0x18U
#define AIC_QSPI_FSTS            0x1CU
#define AIC_QSPI_CCFG            0x24U
#define AIC_QSPI_TBC             0x30U
#define AIC_QSPI_TWC             0x34U
#define AIC_QSPI_TMC             0x38U
#define AIC_QSPI_TXD             0x200U
#define AIC_QSPI_RXD             0x300U

#define AIC_QSPI_CFG_ENABLE      BIT(0)
#define AIC_QSPI_CFG_MASTER      BIT(1)
#define AIC_QSPI_CFG_RXFULL_STOP BIT(7)
#define AIC_QSPI_CFG_RESET       BIT(31)

#define AIC_QSPI_TCFG_CPHA       BIT(0)
#define AIC_QSPI_TCFG_CPOL       BIT(1)
#define AIC_QSPI_TCFG_CS_POL     BIT(2)
#define AIC_QSPI_TCFG_CS_NUM     GENMASK(5, 4)
#define AIC_QSPI_TCFG_CS_SW      BIT(6)
#define AIC_QSPI_TCFG_CS_LEVEL   BIT(7)
#define AIC_QSPI_TCFG_DROP_RX    BIT(8)
#define AIC_QSPI_TCFG_LSB        BIT(12)
#define AIC_QSPI_TCFG_TX_DELAY   BIT(14)
#define AIC_QSPI_TCFG_START      BIT(31)

#define AIC_QSPI_FCTL_RX_RESET   BIT(15)
#define AIC_QSPI_FCTL_TX_RESET   BIT(31)
#define AIC_QSPI_FSTS_RX_COUNT   GENMASK(7, 0)
#define AIC_QSPI_FSTS_TX_COUNT   GENMASK(23, 16)
#define AIC_QSPI_ISTS_DONE       BIT(12)
#define AIC_QSPI_ISTS_ERRORS     GENMASK(11, 8)
#define AIC_QSPI_CCFG_DIV2       GENMASK(7, 0)
#define AIC_QSPI_CCFG_DIV1       GENMASK(11, 8)
#define AIC_QSPI_CCFG_DIV_SEL    BIT(12)
#define AIC_QSPI_COUNT_MASK      GENMASK(23, 0)
#define AIC_QSPI_FIFO_DEPTH      64U
#define AIC_QSPI_TIMEOUT_US      100000U

struct artinchip_qspi_config {
	uintptr_t base;
	uint32_t input_hz;
	const struct device *clock_dev;
	clock_control_subsys_t clock_id;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pcfg;
	bool bootloader_configured;
	bool fixed_div2;
	bool use_clock_rate;
};

struct artinchip_qspi_data {
	struct spi_context ctx;
	uint32_t input_hz;
	uint32_t frequency;
};

static inline uint32_t artinchip_qspi_read(const struct artinchip_qspi_config *cfg, uint32_t offset)
{
	return sys_read32(cfg->base + offset);
}

static inline void artinchip_qspi_write(const struct artinchip_qspi_config *cfg, uint32_t offset,
				   uint32_t value)
{
	sys_write32(value, cfg->base + offset);
}

static int artinchip_qspi_wait_mask(const struct artinchip_qspi_config *cfg, uint32_t offset,
			       uint32_t mask, bool set)
{
	for (uint32_t elapsed = 0U; elapsed < AIC_QSPI_TIMEOUT_US; elapsed++) {
		if (!!(artinchip_qspi_read(cfg, offset) & mask) == set) {
			return 0;
		}
		k_busy_wait(1);
	}

	return -ETIMEDOUT;
}

static int artinchip_qspi_wait_rx_count(const struct artinchip_qspi_config *cfg, size_t count)
{
	for (uint32_t elapsed = 0U; elapsed < AIC_QSPI_TIMEOUT_US; elapsed++) {
		uint32_t available = FIELD_GET(AIC_QSPI_FSTS_RX_COUNT,
					       artinchip_qspi_read(cfg, AIC_QSPI_FSTS));

		if (available >= count) {
			return 0;
		}
		k_busy_wait(1);
	}

	return -ETIMEDOUT;
}

static int artinchip_qspi_reset_fifo(const struct artinchip_qspi_config *cfg)
{
	uint32_t value = artinchip_qspi_read(cfg, AIC_QSPI_FCTL);
	uint32_t reset = AIC_QSPI_FCTL_RX_RESET | AIC_QSPI_FCTL_TX_RESET;

	artinchip_qspi_write(cfg, AIC_QSPI_FCTL, value | reset);

	return artinchip_qspi_wait_mask(cfg, AIC_QSPI_FCTL, reset, false);
}

static void artinchip_qspi_set_cs(const struct artinchip_qspi_config *cfg,
			     const struct spi_config *spi_cfg, bool active)
{
	uint32_t value;
	bool active_high = (spi_cfg->operation & SPI_CS_ACTIVE_HIGH) != 0U;
	bool level = active ? active_high : !active_high;

	value = artinchip_qspi_read(cfg, AIC_QSPI_TCFG);
	value &= ~(AIC_QSPI_TCFG_CS_NUM | AIC_QSPI_TCFG_CS_LEVEL);
	value |= FIELD_PREP(AIC_QSPI_TCFG_CS_NUM, spi_cfg->peripheral);
	if (level) {
		value |= AIC_QSPI_TCFG_CS_LEVEL;
	}
	artinchip_qspi_write(cfg, AIC_QSPI_TCFG, value);
}

static void artinchip_qspi_set_frequency(const struct artinchip_qspi_config *cfg,
					 uint32_t input_hz, uint32_t frequency)
{
	uint32_t div1 = 0U;
	uint32_t div2;
	uint32_t div1_hz;
	uint32_t div2_hz;
	uint32_t value;

	if (cfg->fixed_div2) {
		input_hz /= 2U;
	}

	while ((div1 < 15U) && ((input_hz >> div1) > frequency)) {
		div1++;
	}

	div2 = DIV_ROUND_UP((uint64_t)input_hz, 2ULL * frequency);
	div2 = (div2 == 0U) ? 0U : div2 - 1U;
	div2 = MIN(div2, 255U);
	div1_hz = input_hz >> div1;
	div2_hz = input_hz / (2U * (div2 + 1U));

	value = artinchip_qspi_read(cfg, AIC_QSPI_CCFG);
	value &= ~(AIC_QSPI_CCFG_DIV1 | AIC_QSPI_CCFG_DIV2 | AIC_QSPI_CCFG_DIV_SEL);
	if ((div2_hz <= frequency) && (div2_hz > div1_hz)) {
		value |= FIELD_PREP(AIC_QSPI_CCFG_DIV2, div2) | AIC_QSPI_CCFG_DIV_SEL;
	} else {
		value |= FIELD_PREP(AIC_QSPI_CCFG_DIV1, div1);
	}
	artinchip_qspi_write(cfg, AIC_QSPI_CCFG, value);
}

static int artinchip_qspi_configure(const struct device *dev, const struct spi_config *spi_cfg)
{
	const struct artinchip_qspi_config *cfg = dev->config;
	struct artinchip_qspi_data *data = dev->data;
	uint32_t input_hz = data->input_hz / (cfg->fixed_div2 ? 2U : 1U);
	uint32_t value;

	if ((spi_cfg->operation &
	     (SPI_OP_MODE_PERIPHERAL | SPI_MODE_LOOP | SPI_HALF_DUPLEX)) != 0U ||
	    SPI_WORD_SIZE_GET(spi_cfg->operation) != 8U || spi_cfg->peripheral > 3U ||
	    spi_cfg->frequency == 0U ||
	    spi_cfg->frequency < DIV_ROUND_UP(input_hz, BIT(15)) ||
	    spi_cfg->frequency > input_hz) {
		return -ENOTSUP;
	}

	if (IS_ENABLED(CONFIG_SPI_EXTENDED_MODES) &&
	    (spi_cfg->operation & SPI_LINES_MASK) != SPI_LINES_SINGLE) {
		return -ENOTSUP;
	}

	value = artinchip_qspi_read(cfg, AIC_QSPI_TCFG);
	value &= ~(AIC_QSPI_TCFG_CPHA | AIC_QSPI_TCFG_CPOL | AIC_QSPI_TCFG_CS_POL |
		   AIC_QSPI_TCFG_LSB | AIC_QSPI_TCFG_CS_NUM);
	value |= AIC_QSPI_TCFG_CS_SW | AIC_QSPI_TCFG_TX_DELAY;
	value |= FIELD_PREP(AIC_QSPI_TCFG_CS_NUM, spi_cfg->peripheral);
	if ((spi_cfg->operation & SPI_MODE_CPHA) != 0U) {
		value |= AIC_QSPI_TCFG_CPHA;
	}
	if ((spi_cfg->operation & SPI_MODE_CPOL) != 0U) {
		value |= AIC_QSPI_TCFG_CPOL;
	}
	if ((spi_cfg->operation & SPI_CS_ACTIVE_HIGH) == 0U) {
		value |= AIC_QSPI_TCFG_CS_POL;
	}
	if ((spi_cfg->operation & SPI_TRANSFER_LSB) != 0U) {
		value |= AIC_QSPI_TCFG_LSB;
	}
	artinchip_qspi_write(cfg, AIC_QSPI_TCFG, value);

	if (data->frequency != spi_cfg->frequency) {
		artinchip_qspi_set_frequency(cfg, data->input_hz, spi_cfg->frequency);
		data->frequency = spi_cfg->frequency;
	}

	data->ctx.config = spi_cfg;
	return 0;
}

static int artinchip_qspi_transfer_chunk(const struct artinchip_qspi_config *cfg, const uint8_t *tx,
				    uint8_t *rx, bool tx_active, bool rx_active, size_t len)
{
	size_t completed = 0U;
	uint32_t idle_us = 0U;
	uint32_t value;
	bool duplex = tx_active && rx_active;
	int ret;

	ret = artinchip_qspi_reset_fifo(cfg);
	if (ret != 0) {
		return ret;
	}
	artinchip_qspi_write(cfg, AIC_QSPI_ISTS, UINT32_MAX);
	artinchip_qspi_write(cfg, AIC_QSPI_TBC, len & AIC_QSPI_COUNT_MASK);
	artinchip_qspi_write(cfg, AIC_QSPI_TWC, tx_active ? len & AIC_QSPI_COUNT_MASK : 0U);
	artinchip_qspi_write(cfg, AIC_QSPI_TMC, tx_active ? len & AIC_QSPI_COUNT_MASK : 0U);

	value = artinchip_qspi_read(cfg, AIC_QSPI_TCFG);
	if (duplex) {
		value &= ~AIC_QSPI_TCFG_DROP_RX;
	} else {
		value |= AIC_QSPI_TCFG_DROP_RX;
	}
	artinchip_qspi_write(cfg, AIC_QSPI_TCFG, value | AIC_QSPI_TCFG_START);

	while (completed < len) {
		size_t count;

		if (tx_active) {
			count = AIC_QSPI_FIFO_DEPTH -
				FIELD_GET(AIC_QSPI_FSTS_TX_COUNT,
					  artinchip_qspi_read(cfg, AIC_QSPI_FSTS));
			count = MIN(count, len - completed);
			for (size_t i = 0U; i < count; i++) {
				sys_write8(tx != NULL ? tx[completed + i] : 0U,
					   cfg->base + AIC_QSPI_TXD);
			}
		} else {
			count = FIELD_GET(AIC_QSPI_FSTS_RX_COUNT,
					  artinchip_qspi_read(cfg, AIC_QSPI_FSTS));
			count = MIN(count, len - completed);
			for (size_t i = 0U; i < count; i++) {
				uint8_t value8 = sys_read8(cfg->base + AIC_QSPI_RXD);

				if (rx != NULL) {
					rx[completed + i] = value8;
				}
			}
		}

		if (duplex && count != 0U) {
			ret = artinchip_qspi_wait_rx_count(cfg, count);

			if (ret != 0) {
				return ret;
			}
			for (size_t i = 0U; i < count; i++) {
				uint8_t value8 = sys_read8(cfg->base + AIC_QSPI_RXD);

				if (rx != NULL) {
					rx[completed + i] = value8;
				}
			}
		}

		if (count == 0U) {
			if (idle_us++ >= AIC_QSPI_TIMEOUT_US) {
				return -ETIMEDOUT;
			}
			k_busy_wait(1);
			continue;
		}
		idle_us = 0U;
		completed += count;
	}

	if (tx_active) {
		ret = artinchip_qspi_wait_mask(cfg, AIC_QSPI_FSTS, AIC_QSPI_FSTS_TX_COUNT, false);
		if (ret != 0) {
			return ret;
		}
	}

	if (artinchip_qspi_wait_mask(cfg, AIC_QSPI_ISTS, AIC_QSPI_ISTS_DONE, true) != 0) {
		return -ETIMEDOUT;
	}

	value = artinchip_qspi_read(cfg, AIC_QSPI_ISTS);
	artinchip_qspi_write(cfg, AIC_QSPI_ISTS, value);
	return (value & AIC_QSPI_ISTS_ERRORS) != 0U ? -EIO : 0;
}

static int artinchip_qspi_transceive(const struct device *dev, const struct spi_config *spi_cfg,
				const struct spi_buf_set *tx_bufs,
				const struct spi_buf_set *rx_bufs)
{
	const struct artinchip_qspi_config *cfg = dev->config;
	struct artinchip_qspi_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	int ret;

	spi_context_lock(ctx, false, NULL, NULL, spi_cfg);

	ret = artinchip_qspi_configure(dev, spi_cfg);
	if (ret != 0) {
		goto out;
	}

	spi_context_buffers_setup(ctx, tx_bufs, rx_bufs, 1U);
	if (spi_cs_is_gpio(spi_cfg)) {
		spi_context_cs_control(ctx, true);
	} else {
		artinchip_qspi_set_cs(cfg, spi_cfg, true);
	}

	while (spi_context_tx_on(ctx) || spi_context_rx_on(ctx)) {
		size_t len = MIN(spi_context_max_continuous_chunk(ctx), AIC_QSPI_COUNT_MASK);
		bool tx_active = spi_context_tx_on(ctx);
		bool rx_active = spi_context_rx_on(ctx);

		/*
		 * Receive-only bursts must not exceed the RX FIFO depth: with
		 * RXFULL_STOP the controller halts once the FIFO fills and is
		 * not auto-resumed mid-burst, so a single >FIFO receive would
		 * stall. Splitting into FIFO-sized bursts keeps clocking the
		 * (CS-held) flash read continuously and sequentially.
		 */
		if (!tx_active && rx_active) {
			len = MIN(len, AIC_QSPI_FIFO_DEPTH);
		}

		ret = artinchip_qspi_transfer_chunk(cfg, spi_context_tx_buf_on(ctx) ? ctx->tx_buf : NULL,
					      spi_context_rx_buf_on(ctx) ? ctx->rx_buf : NULL,
					      tx_active, rx_active, len);
		if (ret != 0) {
			break;
		}
		spi_context_update_tx(ctx, 1U, len);
		spi_context_update_rx(ctx, 1U, len);
	}

	if (ret == 0 && (spi_cfg->operation & SPI_HOLD_ON_CS) == 0U) {
		if (spi_cs_is_gpio(spi_cfg)) {
			spi_context_cs_control(ctx, false);
		} else {
			artinchip_qspi_set_cs(cfg, spi_cfg, false);
		}
	}

out:
	if (ret < 0) {
		/* Failed transfers must release CS and ownership even with HOLD/LOCK. */
		if (ctx->config != NULL && !spi_cs_is_gpio(ctx->config)) {
			artinchip_qspi_set_cs(cfg, ctx->config, false);
		}
		spi_context_unlock_unconditionally(ctx);
	} else {
		spi_context_release(ctx, ret);
	}
	return ret;
}

static int artinchip_qspi_release(const struct device *dev, const struct spi_config *spi_cfg)
{
	const struct artinchip_qspi_config *cfg = dev->config;
	struct artinchip_qspi_data *data = dev->data;

	if (data->ctx.config != spi_cfg) {
		return -EINVAL;
	}

	if (!spi_cs_is_gpio(spi_cfg)) {
		artinchip_qspi_set_cs(cfg, spi_cfg, false);
	}
	spi_context_unlock_unconditionally(&data->ctx);
	return 0;
}

static int artinchip_qspi_init(const struct device *dev)
{
	const struct artinchip_qspi_config *cfg = dev->config;
	struct artinchip_qspi_data *data = dev->data;
	int ret;

	/* Legacy firmware-configured D12P boards retain their CMU and pin state. */
	if (!cfg->bootloader_configured || !cfg->fixed_div2) {
		if (!device_is_ready(cfg->clock_dev) || !device_is_ready(cfg->reset.dev)) {
			return -ENODEV;
		}
		ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
		if (ret != 0) {
			return ret;
		}
		ret = clock_control_on(cfg->clock_dev, cfg->clock_id);
		if (ret != 0 && ret != -EALREADY) {
			return ret;
		}
	}

	data->input_hz = cfg->input_hz;
	if (cfg->use_clock_rate && !cfg->bootloader_configured) {
		ret = clock_control_get_rate(cfg->clock_dev, cfg->clock_id, &data->input_hz);
		if (ret != 0 || data->input_hz == 0U) {
			return ret != 0 ? ret : -EINVAL;
		}
	}

	if (!cfg->bootloader_configured) {
		ret = reset_line_toggle_dt(&cfg->reset);
		if (ret != 0) {
			return ret;
		}
	}

	if (!cfg->bootloader_configured || cfg->fixed_div2) {
		/* Clear inherited XIP/DMA state without changing the SoC clock or pins. */
		artinchip_qspi_write(cfg, AIC_QSPI_CFG,
			AIC_QSPI_CFG_ENABLE | AIC_QSPI_CFG_MASTER |
			AIC_QSPI_CFG_RXFULL_STOP | AIC_QSPI_CFG_RESET);
		ret = artinchip_qspi_wait_mask(cfg, AIC_QSPI_CFG, AIC_QSPI_CFG_RESET, false);
		if (ret != 0) {
			return ret;
		}
		artinchip_qspi_write(cfg, AIC_QSPI_CFG,
			AIC_QSPI_CFG_ENABLE | AIC_QSPI_CFG_MASTER | AIC_QSPI_CFG_RXFULL_STOP);
		artinchip_qspi_write(cfg, AIC_QSPI_ICR, 0U);
		artinchip_qspi_write(cfg, AIC_QSPI_ISTS, UINT32_MAX);
		ret = artinchip_qspi_reset_fifo(cfg);
		if (ret != 0) {
			return ret;
		}
		artinchip_qspi_write(cfg, AIC_QSPI_FCTL, 32U | (32U << 16));
		artinchip_qspi_write(cfg, AIC_QSPI_TCFG,
			AIC_QSPI_TCFG_CS_SW | AIC_QSPI_TCFG_CS_LEVEL |
			AIC_QSPI_TCFG_DROP_RX | AIC_QSPI_TCFG_TX_DELAY);
	}

	spi_context_unlock_unconditionally(&data->ctx);
	return 0;
}

static DEVICE_API(spi, artinchip_qspi_api) = {
	.transceive = artinchip_qspi_transceive,
	.release = artinchip_qspi_release,
};

#define AIC_QSPI_INIT(n)                                                                         \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static const struct artinchip_qspi_config artinchip_qspi_config_##n = {                            \
		.use_clock_rate = DT_NODE_HAS_COMPAT(DT_DRV_INST(n), artinchip_d12p_qspi) || \
			DT_NODE_HAS_COMPAT(DT_DRV_INST(n), artinchip_d21x_qspi),                  \
		.fixed_div2 = DT_NODE_HAS_COMPAT(DT_DRV_INST(n), artinchip_d12p_qspi),             \
		.base = DT_INST_REG_ADDR(n),                                                      \
		.input_hz = DT_INST_PROP(n, clock_frequency),                                     \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                               \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, clkid),                 \
		.reset = RESET_DT_SPEC_INST_GET(n),                                                \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.bootloader_configured = DT_INST_PROP_OR(n, artinchip_bootloader_configured, false), \
	};                                                                                         \
	static struct artinchip_qspi_data artinchip_qspi_data_##n = {                                      \
		SPI_CONTEXT_INIT_LOCK(artinchip_qspi_data_##n, ctx),                                   \
		SPI_CONTEXT_INIT_SYNC(artinchip_qspi_data_##n, ctx),                                   \
		SPI_CONTEXT_CS_GPIOS_INITIALIZE(DT_DRV_INST(n), ctx)                              \
	};                                                                                         \
	SPI_DEVICE_DT_INST_DEFINE(n, artinchip_qspi_init, NULL, &artinchip_qspi_data_##n,                  \
				  &artinchip_qspi_config_##n, POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,      \
				  &artinchip_qspi_api);

DT_INST_FOREACH_STATUS_OKAY(AIC_QSPI_INIT)
