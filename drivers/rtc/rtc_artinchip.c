/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_rtc

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/irq.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/sys/util.h>

#include "rtc_utils.h"

#define AIC_RTC_CTL            0x0000U
#define AIC_RTC_INIT           0x0004U
#define AIC_RTC_IRQ_EN         0x0008U
#define AIC_RTC_IRQ_STA        0x000CU
#define AIC_RTC_TIME0          0x0020U
#define AIC_RTC_ALARM0         0x0030U
#define AIC_RTC_WR_EN          0x00FCU
#define AIC_RTC_TCNT           0x0800U
#define AIC_RTC_CTL_ALARM_EN   BIT(2)
#define AIC_RTC_CTL_TCNT_EN    BIT(0)
#define AIC_RTC_IRQ_32K_ERR_EN BIT(2)
#define AIC_RTC_IRQ_ALARM_EN   BIT(0)
#define AIC_RTC_IRQ_ALARM      BIT(0)
#define AIC_RTC_WR_EN_KEY      0xACU
#define AIC_RTC_EPOCH          1589932800LL

#define AIC_RTC_TIME_MASK                                                                         \
	(RTC_ALARM_TIME_MASK_SECOND | RTC_ALARM_TIME_MASK_MINUTE | RTC_ALARM_TIME_MASK_HOUR |      \
	 RTC_ALARM_TIME_MASK_MONTHDAY | RTC_ALARM_TIME_MASK_MONTH | RTC_ALARM_TIME_MASK_YEAR)

struct artinchip_rtc_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	void (*irq_config)(const struct device *dev);
};

struct artinchip_rtc_data {
	struct k_spinlock lock;
#ifdef CONFIG_RTC_ALARM
	rtc_alarm_callback alarm_callback;
	void *alarm_user_data;
	bool alarm_pending;
#endif
};

static uint8_t artinchip_rtc_read8(const struct artinchip_rtc_config *config, uint32_t offset)
{
	return sys_read8(config->base + offset);
}

static void artinchip_rtc_write8(const struct artinchip_rtc_config *config, uint32_t offset, uint8_t value)
{
	sys_write8(AIC_RTC_WR_EN_KEY, config->base + AIC_RTC_WR_EN);
	sys_write8(value, config->base + offset);
	sys_write8(0U, config->base + AIC_RTC_WR_EN);
}

static uint32_t artinchip_rtc_read_bytes(const struct artinchip_rtc_config *config, uint32_t offset)
{
	uint32_t value = 0U;

	for (uint32_t i = 0U; i < sizeof(value); i++) {
		value |= (uint32_t)artinchip_rtc_read8(config, offset + (i * sizeof(uint32_t))) << (i * 8U);
	}

	return value;
}

static void artinchip_rtc_write_bytes(const struct artinchip_rtc_config *config, uint32_t offset,
				 uint32_t value)
{
	sys_write8(AIC_RTC_WR_EN_KEY, config->base + AIC_RTC_WR_EN);
	for (uint32_t i = 0U; i < sizeof(value); i++) {
		sys_write8((uint8_t)(value >> (i * 8U)),
			   config->base + offset + (i * sizeof(uint32_t)));
	}
	sys_write8(0U, config->base + AIC_RTC_WR_EN);
}

static int artinchip_rtc_time_to_seconds(const struct rtc_time *timeptr, uint32_t *seconds)
{
	int64_t unix_seconds;

	if ((timeptr == NULL) || !rtc_utils_validate_rtc_time(timeptr, AIC_RTC_TIME_MASK)) {
		return -EINVAL;
	}

	unix_seconds = timeutil_timegm64(rtc_time_to_tm((struct rtc_time *)timeptr));
	if ((unix_seconds < AIC_RTC_EPOCH) ||
	    (unix_seconds > (AIC_RTC_EPOCH + UINT32_MAX))) {
		return -ERANGE;
	}

	*seconds = (uint32_t)(unix_seconds - AIC_RTC_EPOCH);

	return 0;
}

static int artinchip_rtc_seconds_to_time(uint32_t seconds, struct rtc_time *timeptr)
{
	time_t unix_seconds = (time_t)(AIC_RTC_EPOCH + seconds);
	struct tm tm;

	if ((timeptr == NULL) || (gmtime_r(&unix_seconds, &tm) == NULL)) {
		return -EINVAL;
	}

	*timeptr = (struct rtc_time){
		.tm_sec = tm.tm_sec,
		.tm_min = tm.tm_min,
		.tm_hour = tm.tm_hour,
		.tm_mday = tm.tm_mday,
		.tm_mon = tm.tm_mon,
		.tm_year = tm.tm_year,
		.tm_wday = tm.tm_wday,
		.tm_yday = tm.tm_yday,
		.tm_isdst = -1,
		.tm_nsec = 0,
	};

	return 0;
}

static int artinchip_rtc_set_time(const struct device *dev, const struct rtc_time *timeptr)
{
	const struct artinchip_rtc_config *config = dev->config;
	struct artinchip_rtc_data *data = dev->data;
	uint32_t seconds;
	uint8_t ctl;
	int ret;

	ret = artinchip_rtc_time_to_seconds(timeptr, &seconds);
	if (ret != 0) {
		return ret;
	}

	K_SPINLOCK(&data->lock) {
		ctl = artinchip_rtc_read8(config, AIC_RTC_CTL);
		artinchip_rtc_write8(config, AIC_RTC_CTL, ctl & ~AIC_RTC_CTL_TCNT_EN);
		artinchip_rtc_write8(config, AIC_RTC_INIT, 0U);
		artinchip_rtc_write_bytes(config, AIC_RTC_TIME0, seconds);
		artinchip_rtc_write8(config, AIC_RTC_INIT, 1U);
		artinchip_rtc_write8(config, AIC_RTC_CTL, ctl | AIC_RTC_CTL_TCNT_EN);
	}

	return 0;
}

static int artinchip_rtc_get_time(const struct device *dev, struct rtc_time *timeptr)
{
	const struct artinchip_rtc_config *config = dev->config;

	return artinchip_rtc_seconds_to_time(sys_read32(config->base + AIC_RTC_TCNT), timeptr);
}

#ifdef CONFIG_RTC_ALARM
static void artinchip_rtc_alarm_irq_enable(const struct artinchip_rtc_config *config, bool enable)
{
	uint8_t irq_enable = artinchip_rtc_read8(config, AIC_RTC_IRQ_EN);
	uint8_t alarm_irqs = AIC_RTC_IRQ_32K_ERR_EN | AIC_RTC_IRQ_ALARM_EN;

	irq_enable &= ~alarm_irqs;
	if (enable) {
		irq_enable |= alarm_irqs;
	}
	artinchip_rtc_write8(config, AIC_RTC_IRQ_EN, irq_enable);
}

static int artinchip_rtc_alarm_get_supported_fields(const struct device *dev, uint16_t id,
					       uint16_t *mask)
{
	ARG_UNUSED(dev);

	if ((id != 0U) || (mask == NULL)) {
		return -EINVAL;
	}

	*mask = AIC_RTC_TIME_MASK;
	return 0;
}

static int artinchip_rtc_alarm_set_time(const struct device *dev, uint16_t id, uint16_t mask,
				   const struct rtc_time *timeptr)
{
	const struct artinchip_rtc_config *config = dev->config;
	struct artinchip_rtc_data *data = dev->data;
	uint32_t seconds;
	uint8_t ctl;
	int ret;

	if (id != 0U) {
		return -EINVAL;
	}

	if (mask == 0U) {
		K_SPINLOCK(&data->lock) {
			ctl = artinchip_rtc_read8(config, AIC_RTC_CTL);
			artinchip_rtc_write8(config, AIC_RTC_CTL, ctl & ~AIC_RTC_CTL_ALARM_EN);
			artinchip_rtc_alarm_irq_enable(config, false);
			data->alarm_pending = false;
		}
		return 0;
	}

	if (mask != AIC_RTC_TIME_MASK) {
		return -ENOTSUP;
	}

	ret = artinchip_rtc_time_to_seconds(timeptr, &seconds);
	if (ret != 0) {
		return ret;
	}

	K_SPINLOCK(&data->lock) {
		ctl = artinchip_rtc_read8(config, AIC_RTC_CTL);
		artinchip_rtc_write8(config, AIC_RTC_CTL, ctl & ~AIC_RTC_CTL_ALARM_EN);
		artinchip_rtc_write_bytes(config, AIC_RTC_ALARM0, seconds);
		artinchip_rtc_write8(config, AIC_RTC_IRQ_STA, AIC_RTC_IRQ_ALARM);
		artinchip_rtc_alarm_irq_enable(config, true);
		artinchip_rtc_write8(config, AIC_RTC_CTL, ctl | AIC_RTC_CTL_ALARM_EN);
		data->alarm_pending = false;
	}

	return 0;
}

static int artinchip_rtc_alarm_get_time(const struct device *dev, uint16_t id, uint16_t *mask,
				   struct rtc_time *timeptr)
{
	const struct artinchip_rtc_config *config = dev->config;

	if ((id != 0U) || (mask == NULL) || (timeptr == NULL)) {
		return -EINVAL;
	}

	if ((artinchip_rtc_read8(config, AIC_RTC_CTL) & AIC_RTC_CTL_ALARM_EN) == 0U) {
		*mask = 0U;
		return 0;
	}

	*mask = AIC_RTC_TIME_MASK;
	return artinchip_rtc_seconds_to_time(artinchip_rtc_read_bytes(config, AIC_RTC_ALARM0), timeptr);
}

static int artinchip_rtc_alarm_is_pending(const struct device *dev, uint16_t id)
{
	struct artinchip_rtc_data *data = dev->data;
	bool pending;

	if (id != 0U) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		pending = data->alarm_pending;
		data->alarm_pending = false;
	}

	return pending;
}

static int artinchip_rtc_alarm_set_callback(const struct device *dev, uint16_t id,
				       rtc_alarm_callback callback, void *user_data)
{
	struct artinchip_rtc_data *data = dev->data;

	if (id != 0U) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		data->alarm_callback = callback;
		data->alarm_user_data = user_data;
	}

	return 0;
}

static void artinchip_rtc_isr(const struct device *dev)
{
	const struct artinchip_rtc_config *config = dev->config;
	struct artinchip_rtc_data *data = dev->data;
	rtc_alarm_callback callback = NULL;
	void *user_data = NULL;
	uint8_t status;

	status = artinchip_rtc_read8(config, AIC_RTC_IRQ_STA);
	artinchip_rtc_write8(config, AIC_RTC_IRQ_STA, status);

	if ((status & AIC_RTC_IRQ_ALARM) != 0U) {
		K_SPINLOCK(&data->lock) {
			data->alarm_pending = true;
			callback = data->alarm_callback;
			user_data = data->alarm_user_data;
		}

		if (callback != NULL) {
			callback(dev, 0U, user_data);
		}
	}
}
#endif

static int artinchip_rtc_init(const struct device *dev)
{
	const struct artinchip_rtc_config *config = dev->config;
	struct artinchip_rtc_data *data = dev->data;
	uint8_t status;
	int ret;

	if (!device_is_ready(config->clock_dev) || !device_is_ready(config->reset.dev)) {
		return -ENODEV;
	}

	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0) {
		return ret;
	}

	ret = reset_line_deassert_dt(&config->reset);
	if (ret != 0) {
		return ret;
	}

	status = artinchip_rtc_read8(config, AIC_RTC_IRQ_STA);
	if (status != 0U) {
		artinchip_rtc_write8(config, AIC_RTC_IRQ_STA, status);
	}

	artinchip_rtc_write8(config, AIC_RTC_CTL,
			artinchip_rtc_read8(config, AIC_RTC_CTL) | AIC_RTC_CTL_TCNT_EN);

#ifdef CONFIG_RTC_ALARM
	data->alarm_pending = false;
	config->irq_config(dev);
#else
	ARG_UNUSED(data);
#endif

	return 0;
}

static DEVICE_API(rtc, artinchip_rtc_api) = {
	.set_time = artinchip_rtc_set_time,
	.get_time = artinchip_rtc_get_time,
#ifdef CONFIG_RTC_ALARM
	.alarm_get_supported_fields = artinchip_rtc_alarm_get_supported_fields,
	.alarm_set_time = artinchip_rtc_alarm_set_time,
	.alarm_get_time = artinchip_rtc_alarm_get_time,
	.alarm_is_pending = artinchip_rtc_alarm_is_pending,
	.alarm_set_callback = artinchip_rtc_alarm_set_callback,
#endif
};

#ifdef CONFIG_RTC_ALARM
static void artinchip_rtc_irq_config(const struct device *dev)
{
	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), artinchip_rtc_isr, DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));
}
#endif

static const struct artinchip_rtc_config artinchip_rtc_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
#ifdef CONFIG_RTC_ALARM
	.irq_config = artinchip_rtc_irq_config,
#endif
};

static struct artinchip_rtc_data artinchip_rtc_data;

DEVICE_DT_INST_DEFINE(0, artinchip_rtc_init, NULL, &artinchip_rtc_data, &artinchip_rtc_config, POST_KERNEL,
		      CONFIG_RTC_INIT_PRIORITY, &artinchip_rtc_api);
