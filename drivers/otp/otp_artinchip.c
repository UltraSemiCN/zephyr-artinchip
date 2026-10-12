/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_efuse

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/otp.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_EFUSE_CTL       0x000U
#define AIC_EFUSE_ADDR      0x004U
#define AIC_EFUSE_RDATA     0x00CU
#define AIC_EFUSE_TIMING    0x010U
#define AIC_EFUSE_TIMING_24M 0x0402FFD8U
#define AIC_EFUSE_OPCODE    0xA1CU
#define AIC_EFUSE_OPCODE_MASK GENMASK(27, 16)
#define AIC_EFUSE_STATUS_MASK GENMASK(11, 8)
#define AIC_EFUSE_STATUS_IDLE FIELD_PREP(AIC_EFUSE_STATUS_MASK, 2U)
#define AIC_EFUSE_READ_START BIT(4)
#define AIC_EFUSE_BANKS      2U
#define AIC_EFUSE_TIMEOUT_US 1000U

struct artinchip_efuse_config {
	uintptr_t base;
	size_t size;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
};

struct artinchip_efuse_data {
	struct k_mutex lock;
};

static int artinchip_efuse_wait(const struct artinchip_efuse_config *config, uint32_t mask, uint32_t value)
{
	for (uint32_t elapsed = 0U; elapsed < AIC_EFUSE_TIMEOUT_US; elapsed++) {
		if ((sys_read32(config->base + AIC_EFUSE_CTL) & mask) == value) {
			return 0;
		}
		k_busy_wait(1U);
	}

	return -ETIMEDOUT;
}

static int artinchip_efuse_read_word(const struct artinchip_efuse_config *config, uint32_t word,
				 uint32_t *result)
{
	uint32_t words = config->size / sizeof(uint32_t);
	uint32_t value = 0U;
	int ret;

	for (uint32_t bank = 0U; bank < AIC_EFUSE_BANKS; bank++) {
		uint32_t control;

		sys_write32((word + (words * bank)) * sizeof(uint32_t),
			    config->base + AIC_EFUSE_ADDR);
		control = sys_read32(config->base + AIC_EFUSE_CTL);
		control &= ~(AIC_EFUSE_OPCODE_MASK | AIC_EFUSE_READ_START);
		control |= FIELD_PREP(AIC_EFUSE_OPCODE_MASK, AIC_EFUSE_OPCODE) |
			   AIC_EFUSE_READ_START;
		sys_write32(control, config->base + AIC_EFUSE_CTL);

		ret = artinchip_efuse_wait(config, AIC_EFUSE_READ_START, 0U);
		if (ret != 0) {
			return ret;
		}
		value |= sys_read32(config->base + AIC_EFUSE_RDATA);
	}

	*result = value;

	return 0;
}

static int artinchip_efuse_read(const struct device *dev, off_t offset, void *buffer, size_t length)
{
	const struct artinchip_efuse_config *config = dev->config;
	struct artinchip_efuse_data *data = dev->data;
	uint8_t *out = buffer;
	size_t remaining = length;
	int ret = 0;

	if ((buffer == NULL) || (offset < 0) || ((size_t)offset > config->size) ||
	    (length > (config->size - (size_t)offset))) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	while (remaining > 0U) {
		uint32_t value;
		size_t word_offset = (size_t)offset % sizeof(uint32_t);
		size_t count = MIN(remaining, sizeof(uint32_t) - word_offset);

		ret = artinchip_efuse_read_word(config, (uint32_t)offset / sizeof(uint32_t), &value);
		if (ret != 0) {
			break;
		}
		memcpy(out, ((uint8_t *)&value) + word_offset, count);
		out += count;
		offset += count;
		remaining -= count;
	}
	k_mutex_unlock(&data->lock);

	return ret;
}

static int artinchip_efuse_init(const struct device *dev)
{
	const struct artinchip_efuse_config *config = dev->config;
	struct artinchip_efuse_data *data = dev->data;
	int ret;

	k_mutex_init(&data->lock);
	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0) {
		return ret;
	}

	ret = reset_line_toggle_dt(&config->reset);
	if (ret != 0) {
		return ret;
	}

	sys_write32(AIC_EFUSE_TIMING_24M, config->base + AIC_EFUSE_TIMING);

	return artinchip_efuse_wait(config, AIC_EFUSE_STATUS_MASK, AIC_EFUSE_STATUS_IDLE);
}

static DEVICE_API(otp, artinchip_efuse_api) = {
	.read = artinchip_efuse_read,
};

static const struct artinchip_efuse_config artinchip_efuse_config = {
	.base = DT_INST_REG_ADDR(0),
	.size = DT_INST_PROP(0, size),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
};

static struct artinchip_efuse_data artinchip_efuse_data;

DEVICE_DT_INST_DEFINE(0, artinchip_efuse_init, NULL, &artinchip_efuse_data, &artinchip_efuse_config, POST_KERNEL,
		      CONFIG_OTP_INIT_PRIORITY, &artinchip_efuse_api);
