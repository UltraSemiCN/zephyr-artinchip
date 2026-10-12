/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_spienc

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/misc/artinchip_spienc.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_SPIENC_CTL       0x00U
#define AIC_SPIENC_ICR       0x04U
#define AIC_SPIENC_ISR       0x08U
#define AIC_SPIENC_ADDR      0x14U
#define AIC_SPIENC_TWEAK     0x18U
#define AIC_SPIENC_CPOS      0x1CU
#define AIC_SPIENC_CLEN      0x20U
#define AIC_SPIENC_START     BIT(0)
#define AIC_SPIENC_BUS       GENMASK(13, 12)
#define AIC_SPIENC_XIP       BIT(16)
#define AIC_SPIENC_INTR_ALL  GENMASK(5, 0)
#define AIC_SPIENC_ALL_EMPTY BIT(2)

struct artinchip_spienc_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
};

struct artinchip_spienc_data {
	struct k_mutex lock;
};

static uint32_t artinchip_spienc_read(const struct artinchip_spienc_config *config, uint32_t reg)
{
	return sys_read32(config->base + reg);
}

static void artinchip_spienc_write(const struct artinchip_spienc_config *config, uint32_t reg,
			      uint32_t value)
{
	sys_write32(value, config->base + reg);
}

int artinchip_spienc_configure(const struct device *dev,
			       const struct artinchip_spienc_config *request)
{
	const struct artinchip_spienc_config *config = dev->config;
	struct artinchip_spienc_data *data = dev->data;
	uint32_t bus;
	uint32_t ctl;

	if (request == NULL) {
		return -EINVAL;
	}

	if (request->bypass) {
		bus = 0U;
	} else if (request->controller == 0U) {
		bus = 1U;
	} else if (request->controller == 1U) {
		bus = 2U;
	} else {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	ctl = artinchip_spienc_read(config, AIC_SPIENC_CTL);
	ctl &= ~(AIC_SPIENC_BUS | AIC_SPIENC_START);
	ctl |= FIELD_PREP(AIC_SPIENC_BUS, bus);
	artinchip_spienc_write(config, AIC_SPIENC_CTL, ctl);
	artinchip_spienc_write(config, AIC_SPIENC_ADDR, request->address);
	artinchip_spienc_write(config, AIC_SPIENC_CPOS, request->cipher_position);
	artinchip_spienc_write(config, AIC_SPIENC_CLEN, request->cipher_length);
	artinchip_spienc_write(config, AIC_SPIENC_TWEAK,
			  request->hardware_tweak ? 0U : request->tweak);
	k_mutex_unlock(&data->lock);

	return 0;
}

int artinchip_spienc_start(const struct device *dev)
{
	const struct artinchip_spienc_config *config = dev->config;
	struct artinchip_spienc_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	artinchip_spienc_write(config, AIC_SPIENC_ISR, AIC_SPIENC_INTR_ALL);
	artinchip_spienc_write(config, AIC_SPIENC_CTL,
			  artinchip_spienc_read(config, AIC_SPIENC_CTL) | AIC_SPIENC_START);
	k_mutex_unlock(&data->lock);

	return 0;
}

int artinchip_spienc_stop(const struct device *dev)
{
	const struct artinchip_spienc_config *config = dev->config;
	struct artinchip_spienc_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	artinchip_spienc_write(config, AIC_SPIENC_CTL,
			  artinchip_spienc_read(config, AIC_SPIENC_CTL) & ~AIC_SPIENC_START);
	k_mutex_unlock(&data->lock);

	return 0;
}

int artinchip_spienc_set_xip(const struct device *dev, bool enable)
{
	const struct artinchip_spienc_config *config = dev->config;
	struct artinchip_spienc_data *data = dev->data;
	uint32_t ctl;

	k_mutex_lock(&data->lock, K_FOREVER);
	ctl = artinchip_spienc_read(config, AIC_SPIENC_CTL);
	ctl = enable ? (ctl | AIC_SPIENC_XIP) : (ctl & ~AIC_SPIENC_XIP);
	artinchip_spienc_write(config, AIC_SPIENC_CTL, ctl);
	k_mutex_unlock(&data->lock);

	return 0;
}

int artinchip_spienc_is_empty(const struct device *dev)
{
	const struct artinchip_spienc_config *config = dev->config;
	uint32_t status = artinchip_spienc_read(config, AIC_SPIENC_ISR);

	artinchip_spienc_write(config, AIC_SPIENC_ISR, status);
	return (status & AIC_SPIENC_ALL_EMPTY) != 0U;
}

static int artinchip_spienc_init(const struct device *dev)
{
	const struct artinchip_spienc_config *config = dev->config;
	struct artinchip_spienc_data *data = dev->data;
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
	artinchip_spienc_write(config, AIC_SPIENC_ICR, AIC_SPIENC_INTR_ALL);
	artinchip_spienc_write(config, AIC_SPIENC_CTL, 0U);

	return 0;
}

static const struct artinchip_spienc_config artinchip_spienc_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
};

static struct artinchip_spienc_data artinchip_spienc_data;

DEVICE_DT_INST_DEFINE(0, artinchip_spienc_init, NULL, &artinchip_spienc_data, &artinchip_spienc_config,
		      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
