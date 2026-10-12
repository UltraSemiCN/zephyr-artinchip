/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_wdt

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_WDT_CTL          0x000U
#define AIC_WDT_IRQ_EN       0x008U
#define AIC_WDT_IRQ_STA      0x00CU
#define AIC_WDT_RST_SEL      0x020U
#define AIC_WDT_IRQ_THD      0x044U
#define AIC_WDT_RST_THD      0x048U
#define AIC_WDT_OP           0x0E8U

#define AIC_WDT_CTL_DBG_CONTINUE BIT(1)
#define AIC_WDT_CTL_ENABLE       BIT(0)
#define AIC_WDT_IRQ_TIMEOUT      BIT(0)
#define AIC_WDT_RST_CPU          1U
#define AIC_WDT_RST_SOC          0U
#define AIC_WDT_COUNTER_HZ       32000U
#define AIC_WDT_FEED_CMD0        0xA1C55555U
#define AIC_WDT_FEED_CMD1        0xA1CAAAAAU

struct artinchip_wdt_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	void (*irq_config)(const struct device *dev);
};

struct artinchip_wdt_data {
	wdt_callback_t callback;
	uint32_t timeout_ticks;
	uint8_t reset;
	bool installed;
	bool running;
};

static void artinchip_wdt_feed_hw(const struct artinchip_wdt_config *config)
{
	sys_write32(AIC_WDT_FEED_CMD0, config->base + AIC_WDT_OP);
	sys_write32(AIC_WDT_FEED_CMD1, config->base + AIC_WDT_OP);
}

static int artinchip_wdt_setup(const struct device *dev, uint8_t options)
{
	const struct artinchip_wdt_config *config = dev->config;
	struct artinchip_wdt_data *data = dev->data;
	uint32_t control = AIC_WDT_CTL_ENABLE;

	if (!data->installed) {
		return -EINVAL;
	}

	if (data->running) {
		return -EBUSY;
	}

	if ((options & WDT_OPT_PAUSE_IN_SLEEP) != 0U) {
		return -ENOTSUP;
	}

	if ((options & WDT_OPT_PAUSE_HALTED_BY_DBG) == 0U) {
		control |= AIC_WDT_CTL_DBG_CONTINUE;
	}

	sys_write32(data->timeout_ticks, config->base + AIC_WDT_RST_THD);
	sys_write32(data->callback != NULL ? data->timeout_ticks : 0U,
		    config->base + AIC_WDT_IRQ_THD);
	sys_write32(data->reset, config->base + AIC_WDT_RST_SEL);
	sys_write32(data->callback != NULL ? AIC_WDT_IRQ_TIMEOUT : 0U,
		    config->base + AIC_WDT_IRQ_EN);
	artinchip_wdt_feed_hw(config);
	sys_write32(control, config->base + AIC_WDT_CTL);
	data->running = true;

	return 0;
}

static int artinchip_wdt_disable(const struct device *dev)
{
	const struct artinchip_wdt_config *config = dev->config;
	struct artinchip_wdt_data *data = dev->data;

	if (!data->running) {
		return -EFAULT;
	}

	sys_write32(0U, config->base + AIC_WDT_IRQ_EN);
	sys_write32(sys_read32(config->base + AIC_WDT_CTL) & ~AIC_WDT_CTL_ENABLE,
		    config->base + AIC_WDT_CTL);
	data->installed = false;
	data->running = false;

	return 0;
}

static int artinchip_wdt_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	struct artinchip_wdt_data *data = dev->data;
	uint64_t ticks;

	if (data->running) {
		return -EBUSY;
	}

	if (data->installed) {
		return -ENOMEM;
	}

	if ((cfg->window.min != 0U) || (cfg->window.max == 0U)) {
		return -EINVAL;
	}

	if ((cfg->flags != WDT_FLAG_RESET_NONE) && (cfg->flags != WDT_FLAG_RESET_CPU_CORE) &&
	    (cfg->flags != WDT_FLAG_RESET_SOC)) {
		return -ENOTSUP;
	}

	if ((cfg->callback != NULL) && (cfg->flags != WDT_FLAG_RESET_NONE)) {
		return -ENOTSUP;
	}

	ticks = DIV_ROUND_UP((uint64_t)cfg->window.max * AIC_WDT_COUNTER_HZ, 1000U);
	if ((ticks == 0U) || (ticks > UINT32_MAX)) {
		return -EINVAL;
	}

	data->callback = cfg->callback;
	data->timeout_ticks = (uint32_t)ticks;
	data->reset = cfg->flags == WDT_FLAG_RESET_CPU_CORE ? AIC_WDT_RST_CPU :
							      AIC_WDT_RST_SOC;
	data->installed = true;

	return 0;
}

static int artinchip_wdt_feed(const struct device *dev, int channel_id)
{
	const struct artinchip_wdt_config *config = dev->config;
	struct artinchip_wdt_data *data = dev->data;

	if ((channel_id != 0) || !data->running) {
		return -EINVAL;
	}

	artinchip_wdt_feed_hw(config);

	return 0;
}

static void artinchip_wdt_isr(const struct device *dev)
{
	const struct artinchip_wdt_config *config = dev->config;
	struct artinchip_wdt_data *data = dev->data;
	uint32_t status = sys_read32(config->base + AIC_WDT_IRQ_STA);

	sys_write32(status, config->base + AIC_WDT_IRQ_STA);
	if (((status & AIC_WDT_IRQ_TIMEOUT) != 0U) && (data->callback != NULL)) {
		data->callback(dev, 0);
	}
}

static int artinchip_wdt_init(const struct device *dev)
{
	const struct artinchip_wdt_config *config = dev->config;
	int ret;

	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0) {
		return ret;
	}

	ret = reset_line_toggle_dt(&config->reset);
	if (ret != 0) {
		return ret;
	}

	sys_write32(0U, config->base + AIC_WDT_IRQ_EN);
	sys_write32(0U, config->base + AIC_WDT_CTL);
	config->irq_config(dev);

	return 0;
}

static void artinchip_wdt_irq_config(const struct device *dev)
{
	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), artinchip_wdt_isr, DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));
}

static DEVICE_API(wdt, artinchip_wdt_api) = {
	.setup = artinchip_wdt_setup,
	.disable = artinchip_wdt_disable,
	.install_timeout = artinchip_wdt_install_timeout,
	.feed = artinchip_wdt_feed,
};

static const struct artinchip_wdt_config artinchip_wdt_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
	.irq_config = artinchip_wdt_irq_config,
};

static struct artinchip_wdt_data artinchip_wdt_data;

DEVICE_DT_INST_DEFINE(0, artinchip_wdt_init, NULL, &artinchip_wdt_data, &artinchip_wdt_config, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &artinchip_wdt_api);
