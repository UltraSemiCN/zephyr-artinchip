/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_cir

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/misc/artinchip_cir.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <string.h>

#define AIC_CIR_MCR          0x00U
#define AIC_CIR_INTR         0x04U
#define AIC_CIR_INTEN        0x08U
#define AIC_CIR_TXSTAT       0x0CU
#define AIC_CIR_RXSTAT       0x10U
#define AIC_CIR_RXCLK        0x14U
#define AIC_CIR_RX_THRES     0x18U
#define AIC_CIR_RX_CFG       0x1CU
#define AIC_CIR_RXFIFO       0x30U
#define AIC_CIR_RX_ENABLE    BIT(0)
#define AIC_CIR_RXFIFO_CLEAR BIT(16)
#define AIC_CIR_RX_AVAILABLE BIT(2)
#define AIC_CIR_RX_END       BIT(1)
#define AIC_CIR_RX_OVERFLOW  BIT(0)
#define AIC_CIR_TX_ACTIVE    BIT(11)
#define AIC_CIR_RX_ACTIVE    BIT(11)
#define AIC_CIR_RX_EMPTY     BIT(8)
#define AIC_CIR_RX_COUNT     GENMASK(5, 0)
#define AIC_CIR_BUFFER_SIZE  128U

struct artinchip_cir_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	void (*irq_config)(void);
};

struct artinchip_cir_data {
	struct k_spinlock lock;
	uint8_t buffer[AIC_CIR_BUFFER_SIZE];
	size_t length;
	artinchip_cir_callback_t callback;
	void *user_data;
};

static void artinchip_cir_isr(const struct device *dev)
{
	const struct artinchip_cir_config *config = dev->config;
	struct artinchip_cir_data *data = dev->data;
	uint32_t status = sys_read32(config->base + AIC_CIR_INTR);
	uint32_t rxstat = sys_read32(config->base + AIC_CIR_RXSTAT);
	artinchip_cir_callback_t callback = NULL;
	void *user_data = NULL;

	sys_write32(status, config->base + AIC_CIR_INTR);
	K_SPINLOCK(&data->lock) {
		if ((status & AIC_CIR_RX_OVERFLOW) != 0U) {
			data->length = 0U;
			sys_write32(sys_read32(config->base + AIC_CIR_MCR) |
					    AIC_CIR_RXFIFO_CLEAR,
				    config->base + AIC_CIR_MCR);
		} else if ((status & (AIC_CIR_RX_AVAILABLE | AIC_CIR_RX_END)) != 0U &&
			   (rxstat & AIC_CIR_RX_EMPTY) == 0U) {
			uint32_t count = MIN(FIELD_GET(AIC_CIR_RX_COUNT, rxstat),
					     AIC_CIR_BUFFER_SIZE - data->length);

			for (uint32_t i = 0U; i < count; i++) {
				data->buffer[data->length++] =
					sys_read32(config->base + AIC_CIR_RXFIFO);
			}
		}
		if ((status & AIC_CIR_RX_END) != 0U) {
			callback = data->callback;
			user_data = data->user_data;
		}
	}
	if (callback != NULL) {
		callback(dev, user_data);
	}
}

int artinchip_cir_set_callback(const struct device *dev, artinchip_cir_callback_t callback,
			       void *user_data)
{
	struct artinchip_cir_data *data = dev->data;

	K_SPINLOCK(&data->lock) {
		data->callback = callback;
		data->user_data = user_data;
	}
	return 0;
}

int artinchip_cir_read(const struct device *dev, uint8_t *pulses, size_t size)
{
	struct artinchip_cir_data *data = dev->data;
	size_t length;

	if ((pulses == NULL) || (size == 0U)) {
		return -EINVAL;
	}
	K_SPINLOCK(&data->lock) {
		length = MIN(size, data->length);
		memcpy(pulses, data->buffer, length);
		data->length = 0U;
	}
	return length;
}

bool artinchip_cir_is_busy(const struct device *dev)
{
	const struct artinchip_cir_config *config = dev->config;

	return (sys_read32(config->base + AIC_CIR_TXSTAT) & AIC_CIR_TX_ACTIVE) != 0U ||
	       (sys_read32(config->base + AIC_CIR_RXSTAT) & AIC_CIR_RX_ACTIVE) != 0U;
}

static int artinchip_cir_init(const struct device *dev)
{
	const struct artinchip_cir_config *config = dev->config;
	uint32_t rate;
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
	ret = clock_control_get_rate(config->clock_dev, config->clock_subsys, &rate);
	if (ret != 0) {
		return ret;
	}
	sys_write32(AIC_CIR_RXFIFO_CLEAR, config->base + AIC_CIR_MCR);
	sys_write32(DIV_ROUND_UP(rate, 38000U) - 1U, config->base + AIC_CIR_RXCLK);
	sys_write32((0x10U << 16) | 0x1C0U, config->base + AIC_CIR_RX_THRES);
	sys_write32(0x1770U << 16, config->base + AIC_CIR_RX_CFG);
	sys_write32(0x20U << 8 | AIC_CIR_RX_AVAILABLE | AIC_CIR_RX_END |
			    AIC_CIR_RX_OVERFLOW,
		    config->base + AIC_CIR_INTEN);
	sys_write32(UINT8_MAX, config->base + AIC_CIR_INTR);
	sys_write32(AIC_CIR_RX_ENABLE, config->base + AIC_CIR_MCR);
	config->irq_config();
	return 0;
}

static void artinchip_cir_irq_config(void)
{
	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), artinchip_cir_isr,
		    DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));
}

static const struct artinchip_cir_config artinchip_cir_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
	.irq_config = artinchip_cir_irq_config,
};
static struct artinchip_cir_data artinchip_cir_data;

DEVICE_DT_INST_DEFINE(0, artinchip_cir_init, NULL, &artinchip_cir_data, &artinchip_cir_config, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
