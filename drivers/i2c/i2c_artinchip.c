/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_i2c

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include "i2c-priv.h"

#define AIC_I2C_CTL               0x00U
#define AIC_I2C_TAR               0x04U
#define AIC_I2C_DATA_CMD          0x10U
#define AIC_I2C_SS_SCL_HCNT       0x20U
#define AIC_I2C_SS_SCL_LCNT       0x24U
#define AIC_I2C_FS_SCL_HCNT       0x28U
#define AIC_I2C_FS_SCL_LCNT       0x2CU
#define AIC_I2C_SDA_HOLD          0x30U
#define AIC_I2C_INTR_MASK         0x38U
#define AIC_I2C_INTR_CLR          0x3CU
#define AIC_I2C_INTR_RAW_STAT     0x40U
#define AIC_I2C_ENABLE            0x48U
#define AIC_I2C_ENABLE_STATUS     0x4CU
#define AIC_I2C_TX_ABRT_SOURCE    0x54U
#define AIC_I2C_TXFLR             0x98U
#define AIC_I2C_RXFLR             0x9CU
#define AIC_I2C_SCL_STUCK_TIMEOUT 0xA0U
#define AIC_I2C_SDA_STUCK_TIMEOUT 0xA4U
#define AIC_I2C_FS_SPIKELEN       0xB0U

#define AIC_I2C_CTL_MASTER        BIT(0)
#define AIC_I2C_CTL_SLAVE_DISABLE BIT(1)
#define AIC_I2C_CTL_10BIT_MASTER  BIT(2)
#define AIC_I2C_CTL_SPEED_MASK    GENMASK(5, 4)
#define AIC_I2C_CTL_SPEED_STD     FIELD_PREP(AIC_I2C_CTL_SPEED_MASK, 1U)
#define AIC_I2C_CTL_SPEED_FAST    FIELD_PREP(AIC_I2C_CTL_SPEED_MASK, 2U)
#define AIC_I2C_CTL_RESTART       BIT(6)
#define AIC_I2C_CTL_BUS_CLEAR     BIT(10)

#define AIC_I2C_DATA_READ         BIT(8)
#define AIC_I2C_DATA_STOP         BIT(9)
#define AIC_I2C_DATA_RESTART      BIT(10)

#define AIC_I2C_INTR_TX_ABRT      BIT(6)
#define AIC_I2C_INTR_STOP_DET     BIT(9)

#define AIC_I2C_ENABLE_BIT        BIT(0)
#define AIC_I2C_SDA_RECOVERY      BIT(3)
#define AIC_I2C_TIMEOUT_MS        100
#define AIC_I2C_SDA_HOLD_CYCLES   10U

struct artinchip_i2c_config {
	uintptr_t base;
	uint32_t bitrate;
	uint32_t stuck_time_cycles;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pinctrl;
};

struct artinchip_i2c_data {
	struct k_mutex lock;
	uint32_t dev_config;
	uint32_t input_clock;
};

static int artinchip_i2c_wait_mask(const struct artinchip_i2c_config *config, uint32_t reg, uint32_t mask,
			      bool set)
{
	int64_t deadline = k_uptime_get() + AIC_I2C_TIMEOUT_MS;

	while (((sys_read32(config->base + reg) & mask) != 0U) != set) {
		if (k_uptime_get() >= deadline) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int artinchip_i2c_check_abort(const struct artinchip_i2c_config *config)
{
	if ((sys_read32(config->base + AIC_I2C_INTR_RAW_STAT) & AIC_I2C_INTR_TX_ABRT) != 0U) {
		(void)sys_read32(config->base + AIC_I2C_TX_ABRT_SOURCE);
		sys_write32(AIC_I2C_INTR_TX_ABRT, config->base + AIC_I2C_INTR_CLR);
		return -EIO;
	}

	return 0;
}

static int artinchip_i2c_disable(const struct artinchip_i2c_config *config)
{
	sys_write32(0U, config->base + AIC_I2C_ENABLE);

	return artinchip_i2c_wait_mask(config, AIC_I2C_ENABLE_STATUS, AIC_I2C_ENABLE_BIT, false);
}

static int artinchip_i2c_enable(const struct artinchip_i2c_config *config)
{
	sys_write32(AIC_I2C_ENABLE_BIT | AIC_I2C_SDA_RECOVERY, config->base + AIC_I2C_ENABLE);

	return artinchip_i2c_wait_mask(config, AIC_I2C_ENABLE_STATUS, AIC_I2C_ENABLE_BIT, true);
}

static int artinchip_i2c_set_speed(const struct device *dev, uint32_t speed)
{
	const struct artinchip_i2c_config *config = dev->config;
	struct artinchip_i2c_data *data = dev->data;
	uint32_t cycles;
	uint32_t ctl;
	uint32_t hcnt;
	uint32_t lcnt;
	uint32_t bitrate;
	uint32_t spike_len;

	switch (speed) {
	case I2C_SPEED_STANDARD:
		bitrate = I2C_BITRATE_STANDARD;
		break;
	case I2C_SPEED_FAST:
		bitrate = I2C_BITRATE_FAST;
		break;
	default:
		return -ENOTSUP;
	}

	cycles = data->input_clock / bitrate;
	spike_len = data->input_clock < MHZ(24) ? 2U : 3U;
	lcnt = DIV_ROUND_UP(cycles, 2U) - 1U;
	hcnt = cycles - lcnt - 8U - spike_len;

	if ((hcnt == 0U) || (lcnt == 0U)) {
		return -EINVAL;
	}

	ctl = sys_read32(config->base + AIC_I2C_CTL);
	ctl &= ~AIC_I2C_CTL_SPEED_MASK;
	if (speed == I2C_SPEED_STANDARD) {
		ctl |= AIC_I2C_CTL_SPEED_STD;
		sys_write32(hcnt, config->base + AIC_I2C_SS_SCL_HCNT);
		sys_write32(lcnt, config->base + AIC_I2C_SS_SCL_LCNT);
	} else {
		ctl |= AIC_I2C_CTL_SPEED_FAST;
		sys_write32(hcnt, config->base + AIC_I2C_FS_SCL_HCNT);
		sys_write32(lcnt, config->base + AIC_I2C_FS_SCL_LCNT);
	}
	sys_write32(ctl, config->base + AIC_I2C_CTL);
	sys_write32(spike_len, config->base + AIC_I2C_FS_SPIKELEN);
	sys_write32((uint64_t)config->stuck_time_cycles * data->input_clock / bitrate,
		    config->base + AIC_I2C_SCL_STUCK_TIMEOUT);
	sys_write32((uint64_t)config->stuck_time_cycles * data->input_clock / bitrate,
		    config->base + AIC_I2C_SDA_STUCK_TIMEOUT);

	return 0;
}

static int artinchip_i2c_configure_locked(const struct device *dev, uint32_t dev_config)
{
	const struct artinchip_i2c_config *config = dev->config;
	struct artinchip_i2c_data *data = dev->data;
	uint32_t ctl;
	int ret;

	if ((dev_config & I2C_MODE_CONTROLLER) == 0U) {
		return -ENOTSUP;
	}

	ret = artinchip_i2c_disable(config);
	if (ret != 0) {
		return ret;
	}

	ctl = AIC_I2C_CTL_MASTER | AIC_I2C_CTL_SLAVE_DISABLE | AIC_I2C_CTL_RESTART |
	      AIC_I2C_CTL_BUS_CLEAR;
	sys_write32(ctl, config->base + AIC_I2C_CTL);
	sys_write32(AIC_I2C_SDA_HOLD_CYCLES, config->base + AIC_I2C_SDA_HOLD);

	ret = artinchip_i2c_set_speed(dev, I2C_SPEED_GET(dev_config));
	if (ret == 0) {
		data->dev_config = dev_config;
	}

	return ret;
}

static int artinchip_i2c_configure(const struct device *dev, uint32_t dev_config)
{
	struct artinchip_i2c_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = artinchip_i2c_configure_locked(dev, dev_config);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int artinchip_i2c_get_config(const struct device *dev, uint32_t *dev_config)
{
	struct artinchip_i2c_data *data = dev->data;

	*dev_config = data->dev_config;

	return 0;
}

static int artinchip_i2c_wait_tx_empty(const struct artinchip_i2c_config *config)
{
	int ret;

	ret = artinchip_i2c_wait_mask(config, AIC_I2C_TXFLR, UINT32_MAX, false);
	if (ret != 0) {
		return ret;
	}

	return artinchip_i2c_check_abort(config);
}

static int artinchip_i2c_wait_stop(const struct artinchip_i2c_config *config)
{
	int64_t deadline = k_uptime_get() + AIC_I2C_TIMEOUT_MS;

	while ((sys_read32(config->base + AIC_I2C_INTR_RAW_STAT) &
		AIC_I2C_INTR_STOP_DET) == 0U) {
		if (artinchip_i2c_check_abort(config) != 0) {
			return -EIO;
		}
		if (k_uptime_get() >= deadline) {
			return -ETIMEDOUT;
		}
	}

	sys_write32(AIC_I2C_INTR_STOP_DET, config->base + AIC_I2C_INTR_CLR);

	return artinchip_i2c_check_abort(config);
}

static int artinchip_i2c_read_byte(const struct artinchip_i2c_config *config, uint8_t *byte)
{
	int64_t deadline = k_uptime_get() + AIC_I2C_TIMEOUT_MS;

	while (sys_read32(config->base + AIC_I2C_RXFLR) == 0U) {
		if (artinchip_i2c_check_abort(config) != 0) {
			return -EIO;
		}
		if (k_uptime_get() >= deadline) {
			return -ETIMEDOUT;
		}
	}

	*byte = sys_read32(config->base + AIC_I2C_DATA_CMD);

	return 0;
}

static int artinchip_i2c_transfer(const struct device *dev, struct i2c_msg *msgs, uint8_t num_msgs,
			     uint16_t addr)
{
	const struct artinchip_i2c_config *config = dev->config;
	struct artinchip_i2c_data *data = dev->data;
	bool ten_bit;
	bool bus_active = false;
	int ret = 0;

	if ((msgs == NULL) || (num_msgs == 0U)) {
		return -EINVAL;
	}

	ten_bit = (msgs[0].flags & I2C_MSG_ADDR_10_BITS) != 0U;
	if ((!ten_bit && (addr > 0x7FU)) || (ten_bit && (addr > 0x3FFU))) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = artinchip_i2c_disable(config);
	if (ret != 0) {
		goto out;
	}

	uint32_t ctl = sys_read32(config->base + AIC_I2C_CTL);

	ctl = ten_bit ? (ctl | AIC_I2C_CTL_10BIT_MASTER) : (ctl & ~AIC_I2C_CTL_10BIT_MASTER);
	sys_write32(ctl, config->base + AIC_I2C_CTL);
	sys_write32(addr, config->base + AIC_I2C_TAR);
	sys_write32(0U, config->base + AIC_I2C_INTR_MASK);
	sys_write32(UINT16_MAX, config->base + AIC_I2C_INTR_CLR);

	ret = artinchip_i2c_enable(config);
	if (ret != 0) {
		goto out;
	}

	for (uint8_t msg_idx = 0U; msg_idx < num_msgs; msg_idx++) {
		struct i2c_msg *msg = &msgs[msg_idx];
		bool read = (msg->flags & I2C_MSG_READ) != 0U;
		bool stop = ((msg->flags & I2C_MSG_STOP) != 0U) || (msg_idx == num_msgs - 1U);

		if (((msg->flags & I2C_MSG_ADDR_10_BITS) != 0U) != ten_bit) {
			ret = -EINVAL;
			break;
		}

		if (msg->len == 0U) {
			uint32_t cmd = stop ? AIC_I2C_DATA_STOP : 0U;

			if (bus_active) {
				cmd |= AIC_I2C_DATA_RESTART;
			}
			sys_write32(cmd, config->base + AIC_I2C_DATA_CMD);
			ret = stop ? artinchip_i2c_wait_stop(config) : artinchip_i2c_wait_tx_empty(config);
			bus_active = !stop;
			if (ret != 0) {
				break;
			}
			continue;
		}

		for (uint32_t byte = 0U; byte < msg->len; byte++) {
			uint32_t cmd = read ? AIC_I2C_DATA_READ : msg->buf[byte];

			if ((byte == 0U) && bus_active) {
				cmd |= AIC_I2C_DATA_RESTART;
			}
			if (stop && (byte == msg->len - 1U)) {
				cmd |= AIC_I2C_DATA_STOP;
			}

			sys_write32(cmd, config->base + AIC_I2C_DATA_CMD);
			if (read) {
				ret = artinchip_i2c_read_byte(config, &msg->buf[byte]);
			} else {
				ret = artinchip_i2c_wait_tx_empty(config);
			}
			if (ret != 0) {
				break;
			}
		}

		if ((ret == 0) && stop) {
			ret = artinchip_i2c_wait_stop(config);
		}
		if (ret != 0) {
			break;
		}
		bus_active = !stop;
	}

	(void)artinchip_i2c_disable(config);

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int artinchip_i2c_init(const struct device *dev)
{
	const struct artinchip_i2c_config *config = dev->config;
	struct artinchip_i2c_data *data = dev->data;
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

	ret = pinctrl_apply_state(config->pinctrl, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}

	ret = clock_control_get_rate(config->clock_dev, config->clock_subsys, &data->input_clock);
	if (ret != 0) {
		return ret;
	}

	k_mutex_init(&data->lock);
	sys_write32(0U, config->base + AIC_I2C_INTR_MASK);
	sys_write32(UINT16_MAX, config->base + AIC_I2C_INTR_CLR);

	return artinchip_i2c_configure_locked(dev,
					 I2C_MODE_CONTROLLER | i2c_map_dt_bitrate(config->bitrate));
}

static DEVICE_API(i2c, artinchip_i2c_api) = {
	.configure = artinchip_i2c_configure,
	.get_config = artinchip_i2c_get_config,
	.transfer = artinchip_i2c_transfer,
};

#define AIC_I2C_INIT(inst)									\
	PINCTRL_DT_INST_DEFINE(inst);								\
												\
	static const struct artinchip_i2c_config artinchip_i2c_config_##inst = {				\
		.base = DT_INST_REG_ADDR(inst),							\
		.bitrate = DT_INST_PROP(inst, clock_frequency),					\
		.stuck_time_cycles = DT_INST_PROP(inst, artinchip_stuck_time_cycles),		\
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),				\
		.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),		\
		.reset = RESET_DT_SPEC_INST_GET(inst),						\
		.pinctrl = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),					\
	};											\
												\
	static struct artinchip_i2c_data artinchip_i2c_data_##inst;					\
												\
	I2C_DEVICE_DT_INST_DEFINE(inst, artinchip_i2c_init, NULL, &artinchip_i2c_data_##inst,		\
				  &artinchip_i2c_config_##inst, POST_KERNEL, CONFIG_I2C_INIT_PRIORITY,	\
				  &artinchip_i2c_api);

DT_INST_FOREACH_STATUS_OKAY(AIC_I2C_INIT)
