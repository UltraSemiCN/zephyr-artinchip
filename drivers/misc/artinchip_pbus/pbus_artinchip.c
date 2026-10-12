/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT artinchip_d13x_pbus
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/misc/artinchip_pbus.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>

#define AIC_PBUS_CFG0        0x000U
#define AIC_PBUS_CFG1        0x004U
#define AIC_PBUS_CFG2        0x008U
#define AIC_PBUS_DATA_OFFSET 0x10000U
#define AIC_PBUS_DATA_SIZE   0x10000U

struct artinchip_pbus_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
};
struct artinchip_pbus_data {
	struct k_mutex lock;
};

int artinchip_pbus_configure(const struct device *dev, uint32_t cfg0, uint32_t cfg1,
			     uint32_t cfg2)
{
	const struct artinchip_pbus_config *config = dev->config;
	struct artinchip_pbus_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	sys_write32(cfg0, config->base + AIC_PBUS_CFG0);
	sys_write32(cfg1, config->base + AIC_PBUS_CFG1);
	sys_write32(cfg2, config->base + AIC_PBUS_CFG2);
	k_mutex_unlock(&data->lock);
	return 0;
}

static int artinchip_pbus_transfer(const struct device *dev, uint32_t offset, uint8_t *buffer,
			      size_t length, bool write)
{
	const struct artinchip_pbus_config *config = dev->config;
	struct artinchip_pbus_data *data = dev->data;
	uintptr_t address = config->base + AIC_PBUS_DATA_OFFSET + offset;

	if ((buffer == NULL) || (length == 0U) || (offset + length > AIC_PBUS_DATA_SIZE)) {
		return -EINVAL;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	for (size_t i = 0U; i < length; i++) {
		uintptr_t aligned = (address + i) & ~(uintptr_t)1U;
		uint16_t value = sys_read16(aligned);
		uint32_t shift = ((address + i) & 1U) * 8U;

		if (write) {
			value = (value & ~(UINT16_C(0xFF) << shift)) | ((uint16_t)buffer[i] << shift);
			sys_write16(value, aligned);
		} else {
			buffer[i] = value >> shift;
		}
	}
	k_mutex_unlock(&data->lock);
	return length;
}

int artinchip_pbus_read(const struct device *dev, uint32_t offset, void *buffer, size_t length)
{
	return artinchip_pbus_transfer(dev, offset, buffer, length, false);
}

int artinchip_pbus_write(const struct device *dev, uint32_t offset, const void *buffer,
			  size_t length)
{
	return artinchip_pbus_transfer(dev, offset, (uint8_t *)buffer, length, true);
}

static int artinchip_pbus_init(const struct device *dev)
{
	const struct artinchip_pbus_config *config = dev->config;
	struct artinchip_pbus_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->clock_dev) || !device_is_ready(config->reset.dev)) {
		return -ENODEV;
	}
	k_mutex_init(&data->lock);
	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0) {
		return ret;
	}
	return reset_line_toggle_dt(&config->reset);
}
static const struct artinchip_pbus_config artinchip_pbus_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
};
static struct artinchip_pbus_data artinchip_pbus_data;
DEVICE_DT_INST_DEFINE(0, artinchip_pbus_init, NULL, &artinchip_pbus_data, &artinchip_pbus_config, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
