/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_sdmc

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/kernel.h>
#include <zephyr/sd/sd_spec.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define SDMC_BLKCNT           0x000U
#define SDMC_BLKSIZ           0x004U
#define SDMC_CMDARG           0x008U
#define SDMC_CMD              0x00CU
#define SDMC_RESP0            0x010U
#define SDMC_RESP1            0x014U
#define SDMC_RESP2            0x018U
#define SDMC_RESP3            0x01CU
#define SDMC_TTMC             0x020U
#define SDMC_CTRST            0x02CU
#define SDMC_HCTRL1           0x030U
#define SDMC_CLKCTRL          0x034U
#define SDMC_HCTRL2           0x038U
#define SDMC_INTEN            0x03CU
#define SDMC_OINTST           0x044U
#define SDMC_FIFOCFG          0x048U
#define SDMC_PBUSCFG          0x080U
#define SDMC_CTC              0x100U
#define SDMC_DLYCTRL          0x104U
#define SDMC_FIFO_DATA        0x200U
#define SDMC_CMD_START        BIT(31)
#define SDMC_CMD_HOLD         BIT(29)
#define SDMC_CMD_UPD_CLK      BIT(21)
#define SDMC_CMD_INIT         BIT(15)
#define SDMC_CMD_STOP         BIT(14)
#define SDMC_CMD_WAIT         BIT(13)
#define SDMC_CMD_WRITE        BIT(10)
#define SDMC_CMD_DATA         BIT(9)
#define SDMC_CMD_RESP_CRC     BIT(8)
#define SDMC_CMD_RESP_LONG    BIT(7)
#define SDMC_CMD_RESP         BIT(6)
#define SDMC_CTRST_BUSY       BIT(9)
#define SDMC_CTRST_FIFO_FULL  BIT(3)
#define SDMC_CTRST_FIFO_EMPTY BIT(2)
#define SDMC_HCTRL1_FIFO_RESET BIT(1)
#define SDMC_HCTRL1_RESET_ALL GENMASK(2, 0)
#define SDMC_DATA_ERR         (BIT(15) | BIT(13) | BIT(12) | BIT(11) | BIT(7))
#define SDMC_DATA_TOUT        (BIT(10) | BIT(9))
#define SDMC_CLKCTRL_DIV      GENMASK(15, 8)
#define SDMC_CLKCTRL_DIV_MAX  0xFFU
#define SDMC_CLKCTRL_EN       BIT(0)
#define SDMC_HCTRL2_WIDTH     GENMASK(29, 28)
#define SDMC_HCTRL2_DDR       BIT(16)
#define SDMC_INT_ALL          UINT32_MAX
#define SDMC_INT_ERROR        (BIT(15) | BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(7))
#define SDMC_INT_RTO          BIT(8)
#define SDMC_INT_RCRC         BIT(6)
#define SDMC_INT_RXDR         BIT(5)
#define SDMC_INT_TXDR         BIT(4)
#define SDMC_INT_DATA_DONE    BIT(3)
#define SDMC_INT_CMD_DONE     BIT(2)
#define SDMC_INT_RESP_ERR     BIT(1)
/* MSIZE=2, RX_WMARK=1 (so short reads like SCR still raise RXDR), TX_WMARK=8 */
#define SDMC_FIFOCFG_DEFAULT  (FIELD_PREP(GENMASK(30, 28), 2U) | \
			       FIELD_PREP(GENMASK(26, 16), 1U) | 8U)
#define SDMC_DLY_EXT_CLK_MUX  GENMASK(31, 30)
#define SDMC_DLY_DRIVE_PHASE  GENMASK(29, 28)
#define SDMC_DLY_SAMPLE_PHASE GENMASK(22, 21)
#define SDMC_TIMEOUT_MS       500
#define SDMC_CTC_DEFAULT      (FIELD_PREP(GENMASK(27, 16), 512U) | BIT(0))

struct artinchip_sdmc_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pinctrl;
	uint32_t min_freq;
	uint32_t max_freq;
	uint8_t bus_width;
	uint8_t drive_phase;
	uint8_t sample_phase;
};

struct artinchip_sdmc_data {
	struct k_mutex lock;
	uint32_t source_freq;
};

static uint32_t artinchip_sdmc_read(const struct artinchip_sdmc_config *config, uint32_t reg)
{
	return sys_read32(config->base + reg);
}

static void artinchip_sdmc_write(const struct artinchip_sdmc_config *config, uint32_t reg, uint32_t value)
{
	sys_write32(value, config->base + reg);
}

static int artinchip_sdmc_wait_clear(const struct artinchip_sdmc_config *config, uint32_t reg, uint32_t mask)
{
	return WAIT_FOR((artinchip_sdmc_read(config, reg) & mask) == 0U, SDMC_TIMEOUT_MS * 1000U,
			k_busy_wait(1))
		       ? 0
		       : -ETIMEDOUT;
}

static void artinchip_sdmc_set_phase(const struct artinchip_sdmc_config *config)
{
	uint32_t delay = artinchip_sdmc_read(config, SDMC_DLYCTRL);

	delay &= ~(SDMC_DLY_DRIVE_PHASE | SDMC_DLY_SAMPLE_PHASE);
	delay |= FIELD_PREP(SDMC_DLY_DRIVE_PHASE, config->drive_phase) |
		 FIELD_PREP(SDMC_DLY_SAMPLE_PHASE, config->sample_phase);
	artinchip_sdmc_write(config, SDMC_DLYCTRL, delay);
}

static void artinchip_sdmc_set_ext_clk_mux(const struct artinchip_sdmc_config *config, uint32_t mux)
{
	uint32_t delay = artinchip_sdmc_read(config, SDMC_DLYCTRL);
	uint32_t code;

	/* Match luban: mux 1/2/4 -> DLYCTRL codes 2/1/0 */
	switch (mux) {
	case 1U:
		code = 2U;
		break;
	case 2U:
		code = 1U;
		break;
	case 4U:
	default:
		code = 0U;
		break;
	}

	delay &= ~SDMC_DLY_EXT_CLK_MUX;
	delay |= FIELD_PREP(SDMC_DLY_EXT_CLK_MUX, code);
	artinchip_sdmc_write(config, SDMC_DLYCTRL, delay);
}

static void artinchip_sdmc_hw_setup(const struct artinchip_sdmc_config *config)
{
	artinchip_sdmc_write(config, SDMC_INTEN, 0U);
	artinchip_sdmc_write(config, SDMC_OINTST, SDMC_INT_ALL);
	artinchip_sdmc_write(config, SDMC_TTMC, UINT32_MAX);
	artinchip_sdmc_write(config, SDMC_PBUSCFG, 1U);
	artinchip_sdmc_write(config, SDMC_FIFOCFG, SDMC_FIFOCFG_DEFAULT);
	artinchip_sdmc_write(config, SDMC_CTC, SDMC_CTC_DEFAULT);
	artinchip_sdmc_set_phase(config);
}

static int artinchip_sdmc_reset(const struct device *dev)
{
	const struct artinchip_sdmc_config *config = dev->config;
	int ret;

	artinchip_sdmc_write(config, SDMC_HCTRL1,
			 artinchip_sdmc_read(config, SDMC_HCTRL1) | SDMC_HCTRL1_RESET_ALL);
	ret = artinchip_sdmc_wait_clear(config, SDMC_HCTRL1, SDMC_HCTRL1_RESET_ALL);
	if (ret != 0) {
		return ret;
	}
	artinchip_sdmc_hw_setup(config);
	return 0;
}

static int artinchip_sdmc_update_clock(const struct artinchip_sdmc_config *config)
{
	artinchip_sdmc_write(config, SDMC_CMD, SDMC_CMD_WAIT | SDMC_CMD_UPD_CLK | SDMC_CMD_START);
	return artinchip_sdmc_wait_clear(config, SDMC_CMD, SDMC_CMD_START);
}

static uint32_t artinchip_sdmc_best_div(uint32_t sclk, uint32_t target_freq)
{
	uint32_t down = sclk / target_freq;
	uint32_t up = DIV_ROUND_UP(sclk, target_freq);
	uint32_t f_down = (down == 0U) ? sclk : (sclk / down);
	uint32_t f_up = (up == 0U) ? sclk : (sclk / up);

	return ((f_down - target_freq) < (target_freq - f_up)) ? down : up;
}

static int artinchip_sdmc_set_clock(const struct device *dev, uint32_t frequency)
{
	const struct artinchip_sdmc_config *config = dev->config;
	struct artinchip_sdmc_data *data = dev->data;
	/* Luban: CIU input is CMU/2 before EXT_CLK_MUX + CLKCTRL divider. */
	uint32_t sclk = data->source_freq / 2U;
	uint32_t value;
	uint32_t mux = 1U;
	uint32_t div = 0U;
	int ret;

	value = artinchip_sdmc_read(config, SDMC_CLKCTRL);
	value &= ~SDMC_CLKCTRL_EN;
	artinchip_sdmc_write(config, SDMC_CLKCTRL, value);
	ret = artinchip_sdmc_update_clock(config);
	if ((ret != 0) || (frequency == 0U)) {
		return ret;
	}

	if (sclk == 0U) {
		return -EINVAL;
	}

	if (sclk == frequency) {
		mux = 1U;
		div = 0U;
	} else {
		div = artinchip_sdmc_best_div(sclk, frequency);
		if (div <= 4U) {
			mux = DIV_ROUND_UP(div, 2U);
		} else if ((div % 8U) != 0U) {
			mux = 2U;
		} else {
			mux = 4U;
		}
		div /= (mux * 2U);
		if (div > SDMC_CLKCTRL_DIV_MAX) {
			div = SDMC_CLKCTRL_DIV_MAX;
		}
	}

	artinchip_sdmc_set_ext_clk_mux(config, mux);
	artinchip_sdmc_set_phase(config);

	value = artinchip_sdmc_read(config, SDMC_CLKCTRL);
	value &= ~(SDMC_CLKCTRL_EN | SDMC_CLKCTRL_DIV);
	value |= FIELD_PREP(SDMC_CLKCTRL_DIV, div);
	artinchip_sdmc_write(config, SDMC_CLKCTRL, value);
	ret = artinchip_sdmc_update_clock(config);
	if (ret != 0) {
		return ret;
	}

	artinchip_sdmc_write(config, SDMC_CLKCTRL, value | SDMC_CLKCTRL_EN);
	return artinchip_sdmc_update_clock(config);
}

static int artinchip_sdmc_set_io(const struct device *dev, struct sdhc_io *io)
{
	const struct artinchip_sdmc_config *config = dev->config;
	uint32_t value;
	int ret;

	if ((io->clock > config->max_freq) || ((io->clock != 0U) && (io->clock < config->min_freq))) {
		return -EINVAL;
	}

	switch (io->bus_width) {
	case SDHC_BUS_WIDTH1BIT:
		value = 0U;
		break;
	case SDHC_BUS_WIDTH4BIT:
		value = 1U;
		break;
	case SDHC_BUS_WIDTH8BIT:
		value = 2U;
		break;
	default:
		return -ENOTSUP;
	}

	artinchip_sdmc_write(config, SDMC_HCTRL2,
			 (artinchip_sdmc_read(config, SDMC_HCTRL2) & ~(SDMC_HCTRL2_WIDTH | SDMC_HCTRL2_DDR)) |
				 FIELD_PREP(SDMC_HCTRL2_WIDTH, value) |
				 ((io->timing == SDHC_TIMING_DDR50 ||
				   io->timing == SDHC_TIMING_DDR52)
					  ? SDMC_HCTRL2_DDR
					  : 0U));

	ret = artinchip_sdmc_set_clock(dev, io->clock);
	if (ret != 0) {
		return ret;
	}

	/* Power is not separately switched on this board; only gate the SD clock. */
	ARG_UNUSED(io->power_mode);
	return 0;
}

static bool artinchip_sdmc_is_write(uint32_t opcode)
{
	return (opcode == SD_WRITE_SINGLE_BLOCK) || (opcode == SD_WRITE_MULTIPLE_BLOCK);
}

static uint32_t artinchip_sdmc_cmd_flags(const struct sdhc_command *cmd, const struct sdhc_data *data)
{
	uint32_t flags = SDMC_CMD_START | SDMC_CMD_HOLD | SDMC_CMD_WAIT | cmd->opcode;
	uint32_t response = cmd->response_type & SDHC_NATIVE_RESPONSE_MASK;

	if (data != NULL) {
		flags |= SDMC_CMD_DATA;
		if (artinchip_sdmc_is_write(cmd->opcode)) {
			flags |= SDMC_CMD_WRITE;
		}
	}
	if (cmd->opcode == SD_GO_IDLE_STATE) {
		flags |= SDMC_CMD_INIT;
	}
	if (cmd->opcode == SD_STOP_TRANSMISSION) {
		flags |= SDMC_CMD_STOP;
	}
	if (response != SD_RSP_TYPE_NONE) {
		flags |= SDMC_CMD_RESP;
	}
	if (response == SD_RSP_TYPE_R2) {
		flags |= SDMC_CMD_RESP_LONG | SDMC_CMD_RESP_CRC;
	} else if ((response != SD_RSP_TYPE_R3) && (response != SD_RSP_TYPE_R4) &&
		   (response != SD_RSP_TYPE_NONE)) {
		flags |= SDMC_CMD_RESP_CRC;
	}

	return flags;
}

static int artinchip_sdmc_wait_status(const struct artinchip_sdmc_config *config, uint32_t done,
				 int timeout_ms, uint32_t *status)
{
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(timeout_ms > 0 ? timeout_ms : SDMC_TIMEOUT_MS));

	do {
		*status = artinchip_sdmc_read(config, SDMC_OINTST);
		if ((*status & SDMC_INT_ERROR) != 0U) {
			return ((*status & (SDMC_INT_RTO | BIT(10) | BIT(9))) != 0U) ? -ETIMEDOUT :
										       -EIO;
		}
		if ((*status & done) != 0U) {
			return 0;
		}
		k_busy_wait(1);
	} while (!sys_timepoint_expired(timeout));

	return -ETIMEDOUT;
}

static int artinchip_sdmc_fifo_reset(const struct artinchip_sdmc_config *config)
{
	artinchip_sdmc_write(config, SDMC_HCTRL1,
			     artinchip_sdmc_read(config, SDMC_HCTRL1) | SDMC_HCTRL1_FIFO_RESET);
	return artinchip_sdmc_wait_clear(config, SDMC_HCTRL1, SDMC_HCTRL1_FIFO_RESET);
}

/*
 * PIO transfer matching luban-lite: for short reads (e.g. ACMD51 SCR = 8 bytes)
 * RXDR may never assert if RX watermark is high — must also drain on DAT_DONE.
 * Do not trust FCNT alone; drain while FIFO reports not-empty.
 */
static int artinchip_sdmc_transfer(const struct artinchip_sdmc_config *config, struct sdhc_data *data,
			      bool write)
{
	uint32_t *buffer = data->data;
	uint32_t words = (data->block_size * data->blocks) / sizeof(uint32_t);
	uint32_t status;
	k_timepoint_t timeout;

	if (((data->block_size * data->blocks) % sizeof(uint32_t)) != 0U) {
		return -EINVAL;
	}

	timeout = sys_timepoint_calc(K_MSEC(data->timeout_ms > 0 ? data->timeout_ms : SDMC_TIMEOUT_MS));

	while (words != 0U) {
		status = artinchip_sdmc_read(config, SDMC_OINTST);
		if ((status & (SDMC_DATA_ERR | SDMC_DATA_TOUT)) != 0U) {
			artinchip_sdmc_write(config, SDMC_OINTST, status);
			return ((status & SDMC_DATA_TOUT) != 0U) ? -ETIMEDOUT : -EIO;
		}

		if (write) {
			if ((status & SDMC_INT_TXDR) != 0U) {
				while ((words != 0U) &&
				       ((artinchip_sdmc_read(config, SDMC_CTRST) &
					 SDMC_CTRST_FIFO_FULL) == 0U)) {
					artinchip_sdmc_write(config, SDMC_FIFO_DATA, *buffer++);
					words--;
				}
				artinchip_sdmc_write(config, SDMC_OINTST, SDMC_INT_TXDR);
			}
		} else if (((status & (SDMC_INT_RXDR | SDMC_INT_DATA_DONE)) != 0U) ||
			   ((artinchip_sdmc_read(config, SDMC_CTRST) & SDMC_CTRST_FIFO_EMPTY) ==
			    0U)) {
			/* Clear status first like luban data_rx(), then drain FIFO. */
			artinchip_sdmc_write(config, SDMC_OINTST,
					     SDMC_INT_RXDR | SDMC_INT_DATA_DONE);
			while ((words != 0U) &&
			       ((artinchip_sdmc_read(config, SDMC_CTRST) & SDMC_CTRST_FIFO_EMPTY) ==
				0U)) {
				*buffer++ = artinchip_sdmc_read(config, SDMC_FIFO_DATA);
				words--;
			}
		}

		if (words == 0U) {
			break;
		}

		if (sys_timepoint_expired(timeout)) {
			return -ETIMEDOUT;
		}
		k_busy_wait(1);
	}

	timeout = sys_timepoint_calc(K_MSEC(data->timeout_ms > 0 ? data->timeout_ms : SDMC_TIMEOUT_MS));
	do {
		status = artinchip_sdmc_read(config, SDMC_OINTST);
		if ((status & (SDMC_DATA_ERR | SDMC_DATA_TOUT)) != 0U) {
			artinchip_sdmc_write(config, SDMC_OINTST, status);
			return ((status & SDMC_DATA_TOUT) != 0U) ? -ETIMEDOUT : -EIO;
		}
		if ((status & SDMC_INT_DATA_DONE) != 0U) {
			artinchip_sdmc_write(config, SDMC_OINTST, status);
			data->bytes_xfered = data->block_size * data->blocks;
			return 0;
		}
		/* Short reads may have already cleared DAT_DONE while draining. */
		if ((artinchip_sdmc_read(config, SDMC_CTRST) & SDMC_CTRST_BUSY) == 0U) {
			data->bytes_xfered = data->block_size * data->blocks;
			return 0;
		}
		k_busy_wait(1);
	} while (!sys_timepoint_expired(timeout));

	data->bytes_xfered = data->block_size * data->blocks;
	return 0;
}

static int artinchip_sdmc_request(const struct device *dev, struct sdhc_command *cmd,
			     struct sdhc_data *data)
{
	const struct artinchip_sdmc_config *config = dev->config;
	struct artinchip_sdmc_data *dev_data = dev->data;
	uint32_t status = 0U;
	int ret;

	k_mutex_lock(&dev_data->lock, K_FOREVER);
	if ((cmd->opcode != SD_STOP_TRANSMISSION) &&
	    !WAIT_FOR((artinchip_sdmc_read(config, SDMC_CTRST) & SDMC_CTRST_BUSY) == 0U,
		      SDMC_TIMEOUT_MS * 1000U, k_busy_wait(1))) {
		ret = -EBUSY;
		goto out;
	}

	artinchip_sdmc_write(config, SDMC_OINTST, SDMC_INT_ALL);
	if (data != NULL) {
		ret = artinchip_sdmc_fifo_reset(config);
		if (ret != 0) {
			goto out;
		}
		artinchip_sdmc_write(config, SDMC_BLKSIZ, data->block_size);
		artinchip_sdmc_write(config, SDMC_BLKCNT, data->blocks);
	}
	artinchip_sdmc_write(config, SDMC_CMDARG, cmd->arg);
	artinchip_sdmc_write(config, SDMC_CMD, artinchip_sdmc_cmd_flags(cmd, data));
	ret = artinchip_sdmc_wait_status(config, SDMC_INT_CMD_DONE, cmd->timeout_ms, &status);
	if (ret != 0) {
		goto clear;
	}

	if ((cmd->response_type & SDHC_NATIVE_RESPONSE_MASK) == SD_RSP_TYPE_R2) {
		cmd->response[0] = artinchip_sdmc_read(config, SDMC_RESP3);
		cmd->response[1] = artinchip_sdmc_read(config, SDMC_RESP2);
		cmd->response[2] = artinchip_sdmc_read(config, SDMC_RESP1);
		cmd->response[3] = artinchip_sdmc_read(config, SDMC_RESP0);
	} else {
		cmd->response[0] = artinchip_sdmc_read(config, SDMC_RESP0);
	}

	artinchip_sdmc_write(config, SDMC_OINTST, status & SDMC_INT_CMD_DONE);
	if (data != NULL) {
		ret = artinchip_sdmc_transfer(config, data, artinchip_sdmc_is_write(cmd->opcode));
	}
	goto out;

clear:
	artinchip_sdmc_write(config, SDMC_OINTST, status);
out:
	k_mutex_unlock(&dev_data->lock);
	return ret;
}

static int artinchip_sdmc_get_card_present(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 1;
}

static int artinchip_sdmc_card_busy(const struct device *dev)
{
	const struct artinchip_sdmc_config *config = dev->config;

	return (artinchip_sdmc_read(config, SDMC_CTRST) & SDMC_CTRST_BUSY) != 0U;
}

static int artinchip_sdmc_get_host_props(const struct device *dev, struct sdhc_host_props *props)
{
	const struct artinchip_sdmc_config *config = dev->config;

	memset(props, 0, sizeof(*props));
	props->f_min = config->min_freq;
	props->f_max = config->max_freq;
	props->power_delay = 50U;
	props->bus_4_bit_support = config->bus_width >= 4U;
	props->host_caps.bus_8_bit_support = config->bus_width >= 8U;
	props->host_caps.high_spd_support = true;
	props->host_caps.vol_330_support = true;
	props->is_spi = false;

	return 0;
}

static int artinchip_sdmc_init(const struct device *dev)
{
	const struct artinchip_sdmc_config *config = dev->config;
	struct artinchip_sdmc_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->clock_dev) || !device_is_ready(config->reset.dev)) {
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
	ret = clock_control_get_rate(config->clock_dev, config->clock_subsys, &data->source_freq);
	if (ret != 0) {
		return ret;
	}
	ret = artinchip_sdmc_reset(dev);
	if (ret != 0) {
		return ret;
	}
	return artinchip_sdmc_set_clock(dev, config->min_freq);
}

static DEVICE_API(sdhc, artinchip_sdmc_api) = {
	.reset = artinchip_sdmc_reset,
	.request = artinchip_sdmc_request,
	.set_io = artinchip_sdmc_set_io,
	.get_card_present = artinchip_sdmc_get_card_present,
	.card_busy = artinchip_sdmc_card_busy,
	.get_host_props = artinchip_sdmc_get_host_props,
};

#define AIC_SDMC_PINCTRL(inst)                                                                     \
	COND_CODE_1(DT_INST_PINCTRL_HAS_NAME(inst, default),                                       \
		    (PINCTRL_DT_INST_DEFINE(inst);), ())

#define AIC_SDMC_PINCTRL_PTR(inst)                                                                 \
	COND_CODE_1(DT_INST_PINCTRL_HAS_NAME(inst, default),                                       \
		    (PINCTRL_DT_INST_DEV_CONFIG_GET(inst)), (NULL))

#define AIC_SDMC_DEFINE(inst)                                                                     \
	AIC_SDMC_PINCTRL(inst);                                                                    \
	static const struct artinchip_sdmc_config artinchip_sdmc_config_##inst = {                  \
		.base = DT_INST_REG_ADDR(inst),                                                     \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                              \
		.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),           \
		.reset = RESET_DT_SPEC_INST_GET(inst),                                              \
		.pinctrl = AIC_SDMC_PINCTRL_PTR(inst),                                              \
		.min_freq = DT_INST_PROP(inst, min_bus_freq),                                       \
		.max_freq = DT_INST_PROP(inst, max_bus_freq),                                       \
		.bus_width = DT_INST_PROP(inst, bus_width),                                         \
		.drive_phase = DT_INST_PROP(inst, artinchip_drive_phase),                           \
		.sample_phase = DT_INST_PROP(inst, artinchip_sample_phase),                         \
	};                                                                                         \
	static struct artinchip_sdmc_data artinchip_sdmc_data_##inst;                               \
	DEVICE_DT_INST_DEFINE(inst, artinchip_sdmc_init, NULL, &artinchip_sdmc_data_##inst,         \
			      &artinchip_sdmc_config_##inst, POST_KERNEL, CONFIG_SDHC_INIT_PRIORITY,\
			      &artinchip_sdmc_api);

DT_INST_FOREACH_STATUS_OKAY(AIC_SDMC_DEFINE)
