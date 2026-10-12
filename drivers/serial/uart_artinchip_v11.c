/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#if defined(CONFIG_UART_ARTINCHIP_D12P_NATIVE)
#define DT_DRV_COMPAT artinchip_d12p_uart
#else
#define DT_DRV_COMPAT artinchip_d13x_uart
#endif

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/dt-bindings/dma/artinchip-dma.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(uart_artinchip_v11, CONFIG_UART_LOG_LEVEL);

#define AIC_UART0_BASE           0x18710000U
#define AIC_UART_RBR_THR_DLL     0x00U
#define AIC_UART_DLH_IER         0x04U
#define AIC_UART_IIR_FCR         0x08U
#define AIC_UART_LCR             0x0CU
#define AIC_UART_MCR             0x10U
#define AIC_UART_LSR             0x14U
#define AIC_UART_RFL               0x84U
#define AIC_UART_HSK               0x88U
#define AIC_UART_RXCTL             0xA0U
#define AIC_UART_HALT              0xA4U
#define AIC_UART_RS485CTL          0xC0U
#define AIC_UART_USR               0x7CU

#define AIC_UART_USR_BUSY          BIT(0)
#define AIC_UART_LSR_DR            BIT(0)
#define AIC_UART_LSR_THRE          BIT(5)
#define AIC_UART_LCR_DLAB          BIT(7)
#define AIC_UART_LCR_PEN           BIT(3)
#define AIC_UART_LCR_EPS           BIT(4)
#define AIC_UART_LCR_STOP2         BIT(2)
#define AIC_UART_LCR_WLS8          GENMASK(1, 0)
#define AIC_UART_FCR_FIFO_EN       BIT(0)
#define AIC_UART_FCR_RX_RST        BIT(1)
#define AIC_UART_FCR_TX_RST        BIT(2)
#define AIC_UART_FCR_DMA_MODE      BIT(3)
#define AIC_UART_FCR_RX_TRIG       GENMASK(7, 6)
#define AIC_UART_FCR_TX_TRIG       GENMASK(5, 4)
#define AIC_UART_IER_RDA           BIT(0)
#define AIC_UART_IER_THRE          BIT(1)
#define AIC_UART_IER_RLSI          BIT(2)
#define AIC_UART_RXCTL_EN          BIT(0)
#define AIC_UART_MCR_FLOW_CTRL     0x22U
#define AIC_UART_MCR_FUNC_MASK     0x3FU
#define AIC_UART_MCR_RS485         0x80U
#define AIC_UART_MCR_RS485S        0xC0U
#define AIC_UART_MCR_LOOPBACK      BIT(4)
#define AIC_UART_RS485_RXBFA       BIT(3)
#define AIC_UART_RS485_RXAFA       BIT(2)
#define AIC_UART_RS485_CTL_MODE    BIT(7)
#define AIC_UART_TX_DRQ_EN         BIT(6)
#define AIC_UART_RX_DRQ_EN         BIT(7)
#define AIC_UART_DMA_HSK_MODE      0xE5U
#define AIC_UART_HALT_TX           BIT(0)
#define AIC_UART_IIR_MASK          GENMASK(3, 0)
#define AIC_UART_IIR_THRE          0x02U
#define AIC_UART_IIR_RX            0x04U
#define AIC_UART_IIR_RLS           0x06U
#define AIC_UART_IIR_TIMEOUT       0x0CU
#define AIC_UART_BUSY_TIMEOUT_US   1000000U
/*
 * TX poll timeout. A functional UART asserts THRE within one character time
 * (<100 us at 115200), so this bound is never reached in normal operation.
 * It only matters when the TX path is mis-clocked or otherwise stalled: in
 * that case poll_out must give up quickly and, crucially, must NOT busy-spin
 * for a full second per byte from a cooperative caller (e.g. the MicroPython
 * REPL thread). Busy-spinning there starves the Zephyr shell/log thread on the
 * other UART and freezes the LVGL refresh. Keep it short and yield-friendly.
 */
#define AIC_UART_TX_TIMEOUT_US     4000U
#define AIC_UART_TX_SPIN_US        100U

enum artinchip_uart_rs485_mode {
	AIC_UART_RS485_RS232 = 0,
	AIC_UART_RS485_NORMAL,
	AIC_UART_RS485_COMPACT,
	AIC_UART_RS485_SIMULATION,
};

struct artinchip_uart_baud_clk {
	uint32_t baud;
	uint32_t clk_hz;
};

static const struct artinchip_uart_baud_clk artinchip_uart_baud_table[] = {
	{ 300, 48000000 },      { 600, 48000000 },      { 1200, 48000000 },
	{ 2400, 48000000 },     { 4800, 48000000 },     { 9600, 48000000 },
	{ 14400, 48000000 },    { 19200, 48000000 },    { 38400, 48000000 },
	{ 57600, 48000000 },    { 115200, 48000000 },   { 230400, 48000000 },
	{ 380400, 54545454 },   { 460800, 44444444 },   { 921600, 44444444 },
	{ 1000000, 48000000 },  { 1152000, 54545454 },  { 1500000, 48000000 },
	{ 2500000, 40000000 },  { 3000000, 48000000 },
};

struct artinchip_uart_config {
	uintptr_t base;
	uint8_t port_id;
	enum artinchip_uart_rs485_mode rs485_mode;
	const struct device *clock_dev;
	clock_control_subsys_t clock_id;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pcfg;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	void (*irq_config_func)(const struct device *dev);
#endif
#if defined(CONFIG_UART_ASYNC_API) && defined(CONFIG_DMA_ARTINCHIP)
	const struct device *dma_dev;
	uint8_t dma_tx_channel;
	uint8_t dma_rx_channel;
#endif
};

struct artinchip_uart_data {
	struct uart_config cfg;
	struct k_spinlock lock;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart_irq_callback_user_data_t callback;
	void *callback_data;
#endif
#if defined(CONFIG_UART_ASYNC_API) && defined(CONFIG_DMA_ARTINCHIP)
	uart_callback_t async_cb;
	void *async_user_data;
	const uint8_t *tx_buf;
	size_t tx_len;
	uint8_t *rx_buf;
	size_t rx_len;
#endif
};

static inline uint32_t artinchip_uart_read(const struct device *dev, uint32_t offset)
{
	const struct artinchip_uart_config *config = dev->config;

	return sys_read32(config->base + offset);
}

static inline void artinchip_uart_write(const struct device *dev, uint32_t offset, uint32_t value)
{
	const struct artinchip_uart_config *config = dev->config;

	sys_write32(value, config->base + offset);
}

static uint32_t artinchip_uart_baud_to_clk(uint32_t baudrate)
{
	for (size_t i = 0; i < ARRAY_SIZE(artinchip_uart_baud_table); i++) {
		if (artinchip_uart_baud_table[i].baud == baudrate) {
			return artinchip_uart_baud_table[i].clk_hz;
		}
	}

	return 48000000U;
}

static int artinchip_uart_wait_idle(const struct device *dev)
{
	uint32_t elapsed = 0U;

	while ((artinchip_uart_read(dev, AIC_UART_USR) & AIC_UART_USR_BUSY) != 0U) {
		if (elapsed >= AIC_UART_BUSY_TIMEOUT_US) {
			return -ETIMEDOUT;
		}

		k_busy_wait(1);
		elapsed++;
	}

	return 0;
}

static void artinchip_uart_reset_fifo(const struct device *dev, bool dma_mode)
{
	uint32_t fcr = AIC_UART_FCR_FIFO_EN | AIC_UART_FCR_RX_RST | AIC_UART_FCR_TX_RST;

	if (dma_mode) {
		fcr |= AIC_UART_FCR_DMA_MODE;
		artinchip_uart_write(dev, AIC_UART_IIR_FCR, fcr);
		artinchip_uart_write(dev, AIC_UART_IIR_FCR,
				fcr | FIELD_PREP(AIC_UART_FCR_RX_TRIG, 2U) |
				      FIELD_PREP(AIC_UART_FCR_TX_TRIG, 3U));
	} else {
		artinchip_uart_write(dev, AIC_UART_IIR_FCR, fcr);
		artinchip_uart_write(dev, AIC_UART_IIR_FCR,
				fcr | FIELD_PREP(AIC_UART_FCR_RX_TRIG, 3U) |
				      FIELD_PREP(AIC_UART_FCR_TX_TRIG, 3U));
	}
}

static int artinchip_uart_set_cmu_rate(const struct device *dev, uint32_t baudrate)
{
	const struct artinchip_uart_config *config = dev->config;
	uint32_t clk_hz = artinchip_uart_baud_to_clk(baudrate);
	int ret;

	ret = clock_control_off(config->clock_dev, config->clock_id);
	if (ret != 0 && ret != -ENOSYS && ret != -EALREADY) {
		return ret;
	}

	ret = clock_control_set_rate(config->clock_dev, config->clock_id,
				     (clock_control_subsys_rate_t)&clk_hz);
	if (ret != 0) {
		return ret;
	}

	ret = clock_control_on(config->clock_dev, config->clock_id);
	if (ret != 0 && ret != -EALREADY && ret != -ENOSYS) {
		return ret;
	}

	k_busy_wait(1000);

	return 0;
}

static int artinchip_uart_get_clock_rate(const struct device *dev, uint32_t *rate)
{
	const struct artinchip_uart_config *config = dev->config;

	return clock_control_get_rate(config->clock_dev, config->clock_id, rate);
}

static int artinchip_uart_set_baudrate(const struct device *dev, uint32_t baudrate, uint32_t clk_rate)
{
	uint32_t divisor;
	uint32_t lcr;
	int ret;

	if (baudrate == 0U) {
		return -EINVAL;
	}

	ret = artinchip_uart_wait_idle(dev);
	if (ret != 0) {
		return ret;
	}

	divisor = ((clk_rate * 10U) / baudrate) >> 4;
	if ((divisor % 10U) >= 5U) {
		divisor = (divisor / 10U) + 1U;
	} else {
		divisor /= 10U;
	}

	if (divisor == 0U) {
		return -EINVAL;
	}

	lcr = artinchip_uart_read(dev, AIC_UART_LCR);
	artinchip_uart_write(dev, AIC_UART_LCR, lcr | AIC_UART_LCR_DLAB);
	artinchip_uart_write(dev, AIC_UART_RBR_THR_DLL, divisor & 0xFFU);
	artinchip_uart_write(dev, AIC_UART_DLH_IER, (divisor >> 8) & 0xFFU);
	artinchip_uart_write(dev, AIC_UART_LCR, lcr & ~AIC_UART_LCR_DLAB);

	return 0;
}

static void artinchip_uart_apply_rs485(const struct device *dev, enum artinchip_uart_rs485_mode mode)
{
	uint32_t mcr = artinchip_uart_read(dev, AIC_UART_MCR) & AIC_UART_MCR_FUNC_MASK;
	uint32_t rs485ctl = 0U;

	switch (mode) {
	case AIC_UART_RS485_NORMAL:
	case AIC_UART_RS485_SIMULATION:
		mcr |= AIC_UART_MCR_RS485;
		rs485ctl = AIC_UART_RS485_RXBFA | AIC_UART_RS485_RXAFA;
		if (mode == AIC_UART_RS485_SIMULATION) {
			rs485ctl |= AIC_UART_RS485_CTL_MODE;
		}
		break;
	case AIC_UART_RS485_COMPACT:
		mcr |= AIC_UART_MCR_RS485S;
		rs485ctl = AIC_UART_RS485_RXBFA | AIC_UART_RS485_RXAFA;
		break;
	default:
		break;
	}

	artinchip_uart_write(dev, AIC_UART_MCR, mcr);
	artinchip_uart_write(dev, AIC_UART_RS485CTL, rs485ctl);
}

static int artinchip_uart_apply_line_config(const struct device *dev, const struct uart_config *cfg,
				       bool dma_mode)
{
	const struct artinchip_uart_config *config = dev->config;
	uint32_t lcr = artinchip_uart_read(dev, AIC_UART_LCR) & ~AIC_UART_LCR_DLAB;

	switch (cfg->parity) {
	case UART_CFG_PARITY_NONE:
		lcr &= ~AIC_UART_LCR_PEN;
		break;
	case UART_CFG_PARITY_ODD:
		lcr |= AIC_UART_LCR_PEN;
		lcr &= ~AIC_UART_LCR_EPS;
		break;
	case UART_CFG_PARITY_EVEN:
		lcr |= AIC_UART_LCR_PEN | AIC_UART_LCR_EPS;
		break;
	default:
		return -ENOTSUP;
	}

	switch (cfg->stop_bits) {
	case UART_CFG_STOP_BITS_1:
		lcr &= ~AIC_UART_LCR_STOP2;
		break;
	case UART_CFG_STOP_BITS_2:
		lcr |= AIC_UART_LCR_STOP2;
		break;
	default:
		return -ENOTSUP;
	}

	switch (cfg->data_bits) {
	case UART_CFG_DATA_BITS_5:
		lcr &= ~AIC_UART_LCR_WLS8;
		break;
	case UART_CFG_DATA_BITS_6:
		lcr = (lcr & ~AIC_UART_LCR_WLS8) | 0x01U;
		break;
	case UART_CFG_DATA_BITS_7:
		lcr = (lcr & ~AIC_UART_LCR_WLS8) | 0x02U;
		break;
	case UART_CFG_DATA_BITS_8:
		lcr |= AIC_UART_LCR_WLS8;
		break;
	default:
		return -ENOTSUP;
	}

	artinchip_uart_write(dev, AIC_UART_LCR, lcr);

	if (cfg->flow_ctrl == UART_CFG_FLOW_CTRL_RTS_CTS) {
		artinchip_uart_write(dev, AIC_UART_MCR, AIC_UART_MCR_FLOW_CTRL);
		artinchip_uart_write(dev, AIC_UART_RS485CTL, 0U);
	} else {
		artinchip_uart_apply_rs485(dev, config->rs485_mode);
	}

	artinchip_uart_reset_fifo(dev, dma_mode);
	artinchip_uart_write(dev, AIC_UART_RXCTL, AIC_UART_RXCTL_EN);

	if (dma_mode) {
		artinchip_uart_write(dev, AIC_UART_HSK, AIC_UART_DMA_HSK_MODE);
	}

	return 0;
}

static int artinchip_uart_configure(const struct device *dev, const struct uart_config *cfg)
{
	struct artinchip_uart_data *data = dev->data;
	uint32_t clk_rate;
	k_spinlock_key_t key;
	bool dma_mode = false;
	int ret;

#if defined(CONFIG_UART_ASYNC_API) && defined(CONFIG_DMA_ARTINCHIP)
	const struct artinchip_uart_config *config = dev->config;

	dma_mode = (config->dma_dev != NULL);
#endif

	ret = artinchip_uart_set_cmu_rate(dev, cfg->baudrate);
	if (ret != 0) {
		return ret;
	}

	ret = artinchip_uart_get_clock_rate(dev, &clk_rate);
	if (ret != 0) {
		return ret;
	}

	key = k_spin_lock(&data->lock);

	artinchip_uart_write(dev, AIC_UART_MCR, artinchip_uart_read(dev, AIC_UART_MCR) |
						     AIC_UART_MCR_LOOPBACK);
	artinchip_uart_reset_fifo(dev, dma_mode);

	ret = artinchip_uart_set_baudrate(dev, cfg->baudrate, clk_rate);
	if (ret == 0) {
		ret = artinchip_uart_apply_line_config(dev, cfg, dma_mode);
	}

	artinchip_uart_write(dev, AIC_UART_MCR, artinchip_uart_read(dev, AIC_UART_MCR) &
						     ~AIC_UART_MCR_LOOPBACK);

	if (ret == 0) {
		data->cfg = *cfg;
	}

	k_spin_unlock(&data->lock, key);

	return ret;
}

#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
static int artinchip_uart_config_get(const struct device *dev, struct uart_config *cfg)
{
	const struct artinchip_uart_data *data = dev->data;

	*cfg = data->cfg;

	return 0;
}
#endif

static int artinchip_uart_poll_in(const struct device *dev, unsigned char *c)
{
	if ((artinchip_uart_read(dev, AIC_UART_LSR) & AIC_UART_LSR_DR) == 0U) {
		return -1;
	}

	*c = artinchip_uart_read(dev, AIC_UART_RBR_THR_DLL) & 0xFFU;

	return 0;
}

static void artinchip_uart_poll_out(const struct device *dev, unsigned char c)
{
	uint32_t elapsed = 0U;
	bool can_sleep = k_can_yield();

	while ((artinchip_uart_read(dev, AIC_UART_LSR) & AIC_UART_LSR_THRE) == 0U) {
		if (elapsed >= AIC_UART_TX_TIMEOUT_US) {
			/* TX stalled: drop the byte instead of freezing the CPU. */
			return;
		}

		if (can_sleep && elapsed >= AIC_UART_TX_SPIN_US &&
		    elapsed + 1000U <= AIC_UART_TX_TIMEOUT_US) {
			/* Allow a normal character at 115200 baud to drain before
			 * yielding. Back off only when the transmitter stays busy.
			 */
			k_msleep(1);
			elapsed += 1000U;
		} else {
			k_busy_wait(1);
			elapsed++;
		}
	}

	artinchip_uart_write(dev, AIC_UART_RBR_THR_DLL, c);
}

static int artinchip_uart_err_check(const struct device *dev)
{
	uint32_t lsr = artinchip_uart_read(dev, AIC_UART_LSR);
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
static int artinchip_uart_fifo_fill(const struct device *dev, const uint8_t *tx_data, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if ((artinchip_uart_read(dev, AIC_UART_LSR) & AIC_UART_LSR_THRE) == 0U) {
			break;
		}

		artinchip_uart_write(dev, AIC_UART_RBR_THR_DLL, tx_data[i]);
	}

	return i;
}

static int artinchip_uart_fifo_read(const struct device *dev, uint8_t *rx_data, const int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if ((artinchip_uart_read(dev, AIC_UART_LSR) & AIC_UART_LSR_DR) == 0U) {
			break;
		}

		rx_data[i] = artinchip_uart_read(dev, AIC_UART_RBR_THR_DLL) & 0xFFU;
	}

	return i;
}

static void artinchip_uart_irq_tx_enable(const struct device *dev)
{
	uint32_t ier = artinchip_uart_read(dev, AIC_UART_DLH_IER);

	artinchip_uart_write(dev, AIC_UART_DLH_IER, ier | AIC_UART_IER_THRE);
}

static void artinchip_uart_irq_tx_disable(const struct device *dev)
{
	uint32_t ier = artinchip_uart_read(dev, AIC_UART_DLH_IER);

	artinchip_uart_write(dev, AIC_UART_DLH_IER, ier & ~AIC_UART_IER_THRE);
}

static int artinchip_uart_irq_tx_ready(const struct device *dev)
{
	return (artinchip_uart_read(dev, AIC_UART_LSR) & AIC_UART_LSR_THRE) != 0U;
}

static int artinchip_uart_irq_tx_complete(const struct device *dev)
{
	return artinchip_uart_irq_tx_ready(dev);
}

static void artinchip_uart_irq_rx_enable(const struct device *dev)
{
	uint32_t ier = artinchip_uart_read(dev, AIC_UART_DLH_IER);

	artinchip_uart_write(dev, AIC_UART_DLH_IER, ier | AIC_UART_IER_RDA);
}

static void artinchip_uart_irq_rx_disable(const struct device *dev)
{
	uint32_t ier = artinchip_uart_read(dev, AIC_UART_DLH_IER);

	artinchip_uart_write(dev, AIC_UART_DLH_IER, ier & ~AIC_UART_IER_RDA);
}

static int artinchip_uart_irq_rx_ready(const struct device *dev)
{
	return (artinchip_uart_read(dev, AIC_UART_LSR) & AIC_UART_LSR_DR) != 0U;
}

static void artinchip_uart_irq_err_enable(const struct device *dev)
{
	uint32_t ier = artinchip_uart_read(dev, AIC_UART_DLH_IER);

	artinchip_uart_write(dev, AIC_UART_DLH_IER, ier | AIC_UART_IER_RLSI);
}

static void artinchip_uart_irq_err_disable(const struct device *dev)
{
	uint32_t ier = artinchip_uart_read(dev, AIC_UART_DLH_IER);

	artinchip_uart_write(dev, AIC_UART_DLH_IER, ier & ~AIC_UART_IER_RLSI);
}

static int artinchip_uart_irq_is_pending(const struct device *dev)
{
	uint32_t iir = artinchip_uart_read(dev, AIC_UART_IIR_FCR) & AIC_UART_IIR_MASK;

	return (iir != 0x01U) ? 1 : 0;
}

static void artinchip_uart_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void artinchip_uart_irq_callback_set(const struct device *dev,
				       uart_irq_callback_user_data_t cb, void *user_data)
{
	struct artinchip_uart_data *data = dev->data;

	data->callback = cb;
	data->callback_data = user_data;
}

static void artinchip_uart_isr(const struct device *dev)
{
	struct artinchip_uart_data *data = dev->data;
	uint32_t iir = artinchip_uart_read(dev, AIC_UART_IIR_FCR) & AIC_UART_IIR_MASK;

	switch (iir) {
	case AIC_UART_IIR_RX:
	case AIC_UART_IIR_TIMEOUT:
	case AIC_UART_IIR_RLS:
	case AIC_UART_IIR_THRE:
		if (data->callback != NULL) {
			data->callback(dev, data->callback_data);
		}
		break;
	default:
		break;
	}
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

#if defined(CONFIG_UART_ASYNC_API) && defined(CONFIG_DMA_ARTINCHIP)
static void artinchip_uart_async_user_cb(const struct device *dev, struct uart_event *evt)
{
	struct artinchip_uart_data *data = dev->data;

	if (data->async_cb != NULL) {
		data->async_cb(dev, evt, data->async_user_data);
	}
}

static void artinchip_uart_dma_tx_cb(const struct device *dma_dev, void *user_data, uint32_t channel,
				int status)
{
	const struct device *dev = user_data;
	struct artinchip_uart_data *data = dev->data;
	struct uart_event evt = {
		.type = (status == 0) ? UART_TX_DONE : UART_TX_ABORTED,
		.data.tx.buf = data->tx_buf,
		.data.tx.len = data->tx_len,
	};

	ARG_UNUSED(dma_dev);
	ARG_UNUSED(channel);

	artinchip_uart_write(dev, AIC_UART_HALT,
			artinchip_uart_read(dev, AIC_UART_HALT) & ~AIC_UART_TX_DRQ_EN);
	artinchip_uart_async_user_cb(dev, &evt);
}

static void artinchip_uart_dma_rx_cb(const struct device *dma_dev, void *user_data, uint32_t channel,
				int status)
{
	const struct device *dev = user_data;
	struct artinchip_uart_data *data = dev->data;
	struct uart_event evt = {
		.type = (status == 0) ? UART_RX_RDY : UART_RX_DISABLED,
		.data.rx.buf = data->rx_buf,
		.data.rx.len = data->rx_len,
		.data.rx.offset = 0,
	};

	ARG_UNUSED(dma_dev);
	ARG_UNUSED(channel);

	artinchip_uart_write(dev, AIC_UART_HALT,
			artinchip_uart_read(dev, AIC_UART_HALT) & ~AIC_UART_RX_DRQ_EN);
	artinchip_uart_async_user_cb(dev, &evt);
}

static int artinchip_uart_callback_set(const struct device *dev, uart_callback_t callback,
				  void *user_data)
{
	struct artinchip_uart_data *data = dev->data;

	data->async_cb = callback;
	data->async_user_data = user_data;

	return 0;
}

static int artinchip_uart_tx(const struct device *dev, const uint8_t *buf, size_t len, int32_t timeout)
{
	const struct artinchip_uart_config *config = dev->config;
	struct artinchip_uart_data *data = dev->data;
	struct dma_config dma_cfg = { 0 };
	struct dma_block_config block = { 0 };
	uint32_t dma_id = ARTINCHIP_DMA_ID_UART0 + config->port_id;
	int ret;

	ARG_UNUSED(timeout);

	if (config->dma_dev == NULL) {
		return -ENOTSUP;
	}

	data->tx_buf = buf;
	data->tx_len = len;

	block.source_address = (uintptr_t)buf;
	block.dest_address = config->base + AIC_UART_RBR_THR_DLL;
	block.block_size = len;

	dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
	dma_cfg.dma_slot = dma_id;
	dma_cfg.head_block = &block;
	dma_cfg.block_count = 1;
	dma_cfg.dma_callback = artinchip_uart_dma_tx_cb;
	dma_cfg.user_data = (void *)dev;

	ret = dma_config(config->dma_dev, config->dma_tx_channel, &dma_cfg);
	if (ret != 0) {
		return ret;
	}

	artinchip_uart_write(dev, AIC_UART_HALT,
			artinchip_uart_read(dev, AIC_UART_HALT) | AIC_UART_TX_DRQ_EN);

	return dma_start(config->dma_dev, config->dma_tx_channel);
}

static int artinchip_uart_tx_abort(const struct device *dev)
{
	const struct artinchip_uart_config *config = dev->config;

	if (config->dma_dev == NULL) {
		return -ENOTSUP;
	}

	(void)dma_stop(config->dma_dev, config->dma_tx_channel);
	artinchip_uart_write(dev, AIC_UART_HALT,
			artinchip_uart_read(dev, AIC_UART_HALT) & ~AIC_UART_TX_DRQ_EN);

	return 0;
}

static int artinchip_uart_rx_enable(const struct device *dev, uint8_t *buf, size_t len, int32_t timeout)
{
	const struct artinchip_uart_config *config = dev->config;
	struct artinchip_uart_data *data = dev->data;
	struct dma_config dma_cfg = { 0 };
	struct dma_block_config block = { 0 };
	uint32_t dma_id = ARTINCHIP_DMA_ID_UART0 + config->port_id;
	int ret;

	ARG_UNUSED(timeout);

	if (config->dma_dev == NULL) {
		return -ENOTSUP;
	}

	data->rx_buf = buf;
	data->rx_len = len;

	block.source_address = config->base + AIC_UART_RBR_THR_DLL;
	block.dest_address = (uintptr_t)buf;
	block.block_size = len;

	dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
	dma_cfg.dma_slot = dma_id;
	dma_cfg.head_block = &block;
	dma_cfg.block_count = 1;
	dma_cfg.dma_callback = artinchip_uart_dma_rx_cb;
	dma_cfg.user_data = (void *)dev;

	ret = dma_config(config->dma_dev, config->dma_rx_channel, &dma_cfg);
	if (ret != 0) {
		return ret;
	}

	artinchip_uart_write(dev, AIC_UART_HALT,
			artinchip_uart_read(dev, AIC_UART_HALT) | AIC_UART_RX_DRQ_EN);
	artinchip_uart_write(dev, AIC_UART_DLH_IER,
			artinchip_uart_read(dev, AIC_UART_DLH_IER) | AIC_UART_IER_RDA);

	return dma_start(config->dma_dev, config->dma_rx_channel);
}

static int artinchip_uart_rx_disable(const struct device *dev)
{
	const struct artinchip_uart_config *config = dev->config;

	if (config->dma_dev == NULL) {
		return -ENOTSUP;
	}

	(void)dma_stop(config->dma_dev, config->dma_rx_channel);
	artinchip_uart_write(dev, AIC_UART_HALT,
			artinchip_uart_read(dev, AIC_UART_HALT) & ~AIC_UART_RX_DRQ_EN);
	artinchip_uart_write(dev, AIC_UART_DLH_IER,
			artinchip_uart_read(dev, AIC_UART_DLH_IER) & ~AIC_UART_IER_RDA);

	return 0;
}

static int artinchip_uart_rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len)
{
	return artinchip_uart_rx_enable(dev, buf, len, SYS_FOREVER_US);
}
#endif /* CONFIG_UART_ASYNC_API && CONFIG_DMA_ARTINCHIP */

static DEVICE_API(uart, artinchip_uart_driver_api) = {
	.poll_in = artinchip_uart_poll_in,
	.poll_out = artinchip_uart_poll_out,
	.err_check = artinchip_uart_err_check,
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	.configure = artinchip_uart_configure,
	.config_get = artinchip_uart_config_get,
#endif
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = artinchip_uart_fifo_fill,
	.fifo_read = artinchip_uart_fifo_read,
	.irq_tx_enable = artinchip_uart_irq_tx_enable,
	.irq_tx_disable = artinchip_uart_irq_tx_disable,
	.irq_tx_ready = artinchip_uart_irq_tx_ready,
	.irq_tx_complete = artinchip_uart_irq_tx_complete,
	.irq_rx_enable = artinchip_uart_irq_rx_enable,
	.irq_rx_disable = artinchip_uart_irq_rx_disable,
	.irq_rx_ready = artinchip_uart_irq_rx_ready,
	.irq_err_enable = artinchip_uart_irq_err_enable,
	.irq_err_disable = artinchip_uart_irq_err_disable,
	.irq_is_pending = artinchip_uart_irq_is_pending,
	.irq_update = artinchip_uart_irq_update,
	.irq_callback_set = artinchip_uart_irq_callback_set,
#endif
#if defined(CONFIG_UART_ASYNC_API) && defined(CONFIG_DMA_ARTINCHIP)
	.callback_set = artinchip_uart_callback_set,
	.tx = artinchip_uart_tx,
	.tx_abort = artinchip_uart_tx_abort,
	.rx_enable = artinchip_uart_rx_enable,
	.rx_disable = artinchip_uart_rx_disable,
	.rx_buf_rsp = artinchip_uart_rx_buf_rsp,
#endif
};

static int artinchip_uart_init(const struct device *dev)
{
	const struct artinchip_uart_config *config = dev->config;
	struct artinchip_uart_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->clock_dev)) {
		return -ENODEV;
	}

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}

	if (config->reset.dev != NULL) {
		if (!device_is_ready(config->reset.dev)) {
			return -ENODEV;
		}

		ret = reset_line_toggle_dt(&config->reset);
		if (ret != 0) {
			return ret;
		}
	}

	ret = artinchip_uart_configure(dev, &data->cfg);
	if (ret != 0) {
		LOG_ERR("failed to configure UART (err %d)", ret);
		return ret;
	}

	artinchip_uart_write(dev, AIC_UART_DLH_IER, 0U);

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	config->irq_config_func(dev);
#endif

	return 0;
}

#define AIC_UART_PORT_ID(n) (((DT_INST_REG_ADDR(n) - AIC_UART0_BASE) / 0x1000U))

#define AIC_UART_RS485_MODE(n)                                                                    \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, artinchip_rs485_mode),                                \
		    (DT_INST_ENUM_IDX(n, artinchip_rs485_mode)), (AIC_UART_RS485_RS232))

#define AIC_UART_DMA_CFG(n)                                                                       \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, dmas),                                                   \
		   (.dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, tx)),                    \
		    .dma_tx_channel = DT_INST_DMAS_CELL_BY_NAME(n, tx, channel),                     \
		    .dma_rx_channel = DT_INST_DMAS_CELL_BY_NAME(n, rx, channel),))

#define AIC_UART_IRQ_CONFIG(n)                                                                    \
	static void artinchip_uart_irq_config_func_##n(const struct device *dev)                        \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), artinchip_uart_isr,               \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                         \
	}

#define AIC_UART_IRQ_CFG(n)                                                                       \
	COND_CODE_1(CONFIG_UART_INTERRUPT_DRIVEN,                                                  \
		    (.irq_config_func = artinchip_uart_irq_config_func_##n,), ())

#define AIC_UART_IRQ_INST(n)                                                                      \
	COND_CODE_1(CONFIG_UART_INTERRUPT_DRIVEN, (AIC_UART_IRQ_CONFIG(n)), ())

#define AIC_UART_CFG_INIT(n)                                                                      \
	.baudrate = DT_INST_PROP(n, current_speed),                                                \
	.parity = UART_CFG_PARITY_NONE,                                                            \
	.stop_bits = UART_CFG_STOP_BITS_1,                                                         \
	.data_bits = UART_CFG_DATA_BITS_8,                                                         \
	.flow_ctrl = COND_CODE_1(DT_INST_PROP(n, hw_flow_control),                                  \
				 (UART_CFG_FLOW_CTRL_RTS_CTS), (UART_CFG_FLOW_CTRL_NONE)),

#define AIC_UART_INIT(n)                                                                          \
	PINCTRL_DT_INST_DEFINE(n);                                                                  \
	AIC_UART_IRQ_INST(n)                                                                      \
	static const struct artinchip_uart_config artinchip_uart_config_##n = {                              \
		.base = DT_INST_REG_ADDR(n),                                                      \
		.port_id = AIC_UART_PORT_ID(n),                                                  \
		.rs485_mode = AIC_UART_RS485_MODE(n),                                            \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, clkid),                 \
		.reset = RESET_DT_SPEC_INST_GET(n),                                                \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		AIC_UART_IRQ_CFG(n)                                                               \
		AIC_UART_DMA_CFG(n)};                                                             \
	static struct artinchip_uart_data artinchip_uart_data_##n = {                                        \
		.cfg = { AIC_UART_CFG_INIT(n) },                                                  \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, artinchip_uart_init, NULL, &artinchip_uart_data_##n, &artinchip_uart_config_##n, \
			      PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY, &artinchip_uart_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AIC_UART_INIT)
