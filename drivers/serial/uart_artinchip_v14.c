/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_V14_UART_RBR_THR 0x00U
#define AIC_V14_UART_IER     0x04U
#define AIC_V14_UART_IIR     0x08U
#define AIC_V14_UART_LSR     0x14U

#define AIC_V14_UART_LSR_DR   BIT(0)
#define AIC_V14_UART_LSR_THRE BIT(5)
#define AIC_V14_UART_IER_RDA  BIT(0)
#define AIC_V14_UART_IER_THRE BIT(1)
#define AIC_V14_UART_IER_RLSI BIT(2)
#define AIC_V14_UART_IIR_MASK GENMASK(3, 0)

struct artinchip_v14_uart_config {
	uintptr_t base;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	void (*irq_config_func)(const struct device *dev);
#endif
};

struct artinchip_v14_uart_data {
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart_irq_callback_user_data_t callback;
	void *callback_data;
#endif
};

static uint32_t artinchip_v14_uart_read(const struct device *dev, uint32_t offset)
{
	const struct artinchip_v14_uart_config *config = dev->config;

	return sys_read32(config->base + offset);
}

static void artinchip_v14_uart_write(const struct device *dev, uint32_t offset, uint32_t value)
{
	const struct artinchip_v14_uart_config *config = dev->config;

	sys_write32(value, config->base + offset);
}

static int artinchip_v14_uart_poll_in(const struct device *dev, unsigned char *c)
{
	if ((artinchip_v14_uart_read(dev, AIC_V14_UART_LSR) & AIC_V14_UART_LSR_DR) == 0U) {
		return -1;
	}

	*c = artinchip_v14_uart_read(dev, AIC_V14_UART_RBR_THR) & 0xFFU;
	return 0;
}

static void artinchip_v14_uart_poll_out(const struct device *dev, unsigned char c)
{
	while ((artinchip_v14_uart_read(dev, AIC_V14_UART_LSR) & AIC_V14_UART_LSR_THRE) == 0U) {
	}

	artinchip_v14_uart_write(dev, AIC_V14_UART_RBR_THR, c);
}

static int artinchip_v14_uart_err_check(const struct device *dev)
{
	uint32_t lsr = artinchip_v14_uart_read(dev, AIC_V14_UART_LSR);
	int err = 0;

	if ((lsr & BIT(4)) != 0U) {
		err |= UART_BREAK;
	}
	if ((lsr & BIT(3)) != 0U) {
		err |= UART_ERROR_FRAMING;
	}
	if ((lsr & BIT(2)) != 0U) {
		err |= UART_ERROR_PARITY;
	}
	if ((lsr & BIT(1)) != 0U) {
		err |= UART_ERROR_OVERRUN;
	}

	return err;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static int artinchip_v14_uart_fifo_fill(const struct device *dev, const uint8_t *tx_data, int size)
{
	int count;

	for (count = 0; count < size; count++) {
		if ((artinchip_v14_uart_read(dev, AIC_V14_UART_LSR) & AIC_V14_UART_LSR_THRE) == 0U) {
			break;
		}
		artinchip_v14_uart_write(dev, AIC_V14_UART_RBR_THR, tx_data[count]);
	}

	return count;
}

static int artinchip_v14_uart_fifo_read(const struct device *dev, uint8_t *rx_data, int size)
{
	int count;

	for (count = 0; count < size; count++) {
		if ((artinchip_v14_uart_read(dev, AIC_V14_UART_LSR) & AIC_V14_UART_LSR_DR) == 0U) {
			break;
		}
		rx_data[count] = artinchip_v14_uart_read(dev, AIC_V14_UART_RBR_THR) & 0xFFU;
	}

	return count;
}

static void artinchip_v14_uart_irq_tx_enable(const struct device *dev)
{
	artinchip_v14_uart_write(dev, AIC_V14_UART_IER,
			artinchip_v14_uart_read(dev, AIC_V14_UART_IER) | AIC_V14_UART_IER_THRE);
}

static void artinchip_v14_uart_irq_tx_disable(const struct device *dev)
{
	artinchip_v14_uart_write(dev, AIC_V14_UART_IER,
			artinchip_v14_uart_read(dev, AIC_V14_UART_IER) & ~AIC_V14_UART_IER_THRE);
}

static int artinchip_v14_uart_irq_tx_ready(const struct device *dev)
{
	return (artinchip_v14_uart_read(dev, AIC_V14_UART_LSR) & AIC_V14_UART_LSR_THRE) != 0U;
}

static int artinchip_v14_uart_irq_tx_complete(const struct device *dev)
{
	return artinchip_v14_uart_irq_tx_ready(dev);
}

static void artinchip_v14_uart_irq_rx_enable(const struct device *dev)
{
	artinchip_v14_uart_write(dev, AIC_V14_UART_IER,
			artinchip_v14_uart_read(dev, AIC_V14_UART_IER) | AIC_V14_UART_IER_RDA);
}

static void artinchip_v14_uart_irq_rx_disable(const struct device *dev)
{
	artinchip_v14_uart_write(dev, AIC_V14_UART_IER,
			artinchip_v14_uart_read(dev, AIC_V14_UART_IER) & ~AIC_V14_UART_IER_RDA);
}

static int artinchip_v14_uart_irq_rx_ready(const struct device *dev)
{
	return (artinchip_v14_uart_read(dev, AIC_V14_UART_LSR) & AIC_V14_UART_LSR_DR) != 0U;
}

static void artinchip_v14_uart_irq_err_enable(const struct device *dev)
{
	artinchip_v14_uart_write(dev, AIC_V14_UART_IER,
			artinchip_v14_uart_read(dev, AIC_V14_UART_IER) | AIC_V14_UART_IER_RLSI);
}

static void artinchip_v14_uart_irq_err_disable(const struct device *dev)
{
	artinchip_v14_uart_write(dev, AIC_V14_UART_IER,
			artinchip_v14_uart_read(dev, AIC_V14_UART_IER) & ~AIC_V14_UART_IER_RLSI);
}

static int artinchip_v14_uart_irq_is_pending(const struct device *dev)
{
	return (artinchip_v14_uart_read(dev, AIC_V14_UART_IIR) & AIC_V14_UART_IIR_MASK) != 0x01U;
}

static void artinchip_v14_uart_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void artinchip_v14_uart_irq_callback_set(const struct device *dev,
				       uart_irq_callback_user_data_t cb, void *user_data)
{
	struct artinchip_v14_uart_data *data = dev->data;

	data->callback = cb;
	data->callback_data = user_data;
}

static void artinchip_v14_uart_isr(const struct device *dev)
{
	struct artinchip_v14_uart_data *data = dev->data;

	if (data->callback != NULL) {
		data->callback(dev, data->callback_data);
	}
}
#endif

static DEVICE_API(uart, artinchip_v14_uart_driver_api) = {
	.poll_in = artinchip_v14_uart_poll_in,
	.poll_out = artinchip_v14_uart_poll_out,
	.err_check = artinchip_v14_uart_err_check,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = artinchip_v14_uart_fifo_fill,
	.fifo_read = artinchip_v14_uart_fifo_read,
	.irq_tx_enable = artinchip_v14_uart_irq_tx_enable,
	.irq_tx_disable = artinchip_v14_uart_irq_tx_disable,
	.irq_tx_ready = artinchip_v14_uart_irq_tx_ready,
	.irq_tx_complete = artinchip_v14_uart_irq_tx_complete,
	.irq_rx_enable = artinchip_v14_uart_irq_rx_enable,
	.irq_rx_disable = artinchip_v14_uart_irq_rx_disable,
	.irq_rx_ready = artinchip_v14_uart_irq_rx_ready,
	.irq_err_enable = artinchip_v14_uart_irq_err_enable,
	.irq_err_disable = artinchip_v14_uart_irq_err_disable,
	.irq_is_pending = artinchip_v14_uart_irq_is_pending,
	.irq_update = artinchip_v14_uart_irq_update,
	.irq_callback_set = artinchip_v14_uart_irq_callback_set,
#endif
};

static int artinchip_v14_uart_init(const struct device *dev)
{
	const struct artinchip_v14_uart_config *config = dev->config;

	artinchip_v14_uart_write(dev, AIC_V14_UART_IER, 0U);
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	config->irq_config_func(dev);
#else
	ARG_UNUSED(config);
#endif
	return 0;
}

/* Use node identifiers so different compatible instances cannot collide. */
#define AIC_V14_UART_IRQ_CONFIG(node)                                               \
	static void artinchip_v14_uart_irq_config_##node(const struct device *dev)    \
	{                                                                          \
		ARG_UNUSED(dev);                                                   \
		IRQ_CONNECT(DT_IRQN(node), DT_IRQ(node, priority),                   \
			    artinchip_v14_uart_isr, DEVICE_DT_GET(node), 0);         \
		irq_enable(DT_IRQN(node));                                          \
	}

#define AIC_V14_UART_INIT(node)                                                     \
	IF_ENABLED(CONFIG_UART_INTERRUPT_DRIVEN, (AIC_V14_UART_IRQ_CONFIG(node)))    \
	static const struct artinchip_v14_uart_config config_##node = {              \
		.base = DT_REG_ADDR(node),                                         \
		IF_ENABLED(CONFIG_UART_INTERRUPT_DRIVEN,                           \
			   (.irq_config_func = artinchip_v14_uart_irq_config_##node,)) \
	};                                                                         \
	static struct artinchip_v14_uart_data data_##node;                          \
	DEVICE_DT_DEFINE(node, artinchip_v14_uart_init, NULL, &data_##node,           \
			 &config_##node, PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY,   \
			 &artinchip_v14_uart_driver_api);

#ifndef CONFIG_UART_ARTINCHIP_D12P_NATIVE
DT_FOREACH_STATUS_OKAY(artinchip_d12p_uart, AIC_V14_UART_INIT)
#endif
DT_FOREACH_STATUS_OKAY(artinchip_boot_uart, AIC_V14_UART_INIT)
