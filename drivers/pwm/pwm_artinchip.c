/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_pwm

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_PWM_CHANNELS          4U
#define AIC_PWM_CTL               0x000U
#define AIC_PWM_MCTL              0x004U
#define AIC_PWM_CKCTL             0x008U
#define AIC_PWM_CHANNEL_BASE      0x300U
#define AIC_PWM_CHANNEL_STRIDE    0x100U
#define AIC_PWM_TBCTL             0x000U
#define AIC_PWM_TBPRD             0x014U
#define AIC_PWM_CMPCTL            0x018U
#define AIC_PWM_CMPA              0x020U
#define AIC_PWM_AQCTLA            0x028U

#define AIC_PWM_CTL_ENABLE        BIT(0)
#define AIC_PWM_TBCTL_DIV_MASK    GENMASK(27, 16)
#define AIC_PWM_TBCTL_MODE_MASK   GENMASK(1, 0)
#define AIC_PWM_TBCTL_MODE_UP     0U
#define AIC_PWM_PERIOD_MAX        UINT16_MAX
#define AIC_PWM_DIV_MAX           4096U
#define AIC_PWM_CMP_SHADOW_ZRO_PRD 0xAU

#define AIC_PWM_ACTION_LOW        1U
#define AIC_PWM_ACTION_HIGH       2U
#define AIC_PWM_ACTION_CAU_SHIFT  4U
#define AIC_PWM_ACTION_DEFAULT    BIT(16)

struct artinchip_pwm_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pinctrl;
};

struct artinchip_pwm_data {
	struct k_spinlock lock;
	uint32_t input_clock;
};

static uintptr_t artinchip_pwm_channel_reg(const struct artinchip_pwm_config *config, uint32_t channel,
				      uint32_t reg)
{
	return config->base + AIC_PWM_CHANNEL_BASE + channel * AIC_PWM_CHANNEL_STRIDE + reg;
}

static int artinchip_pwm_set_cycles(const struct device *dev, uint32_t channel, uint32_t period_cycles,
			       uint32_t pulse_cycles, pwm_flags_t flags)
{
	const struct artinchip_pwm_config *config = dev->config;
	struct artinchip_pwm_data *data = dev->data;
	uint32_t period_count;
	uint32_t pulse_count;
	uint32_t divider;
	uint32_t action;
	uint32_t value;

	if ((channel >= AIC_PWM_CHANNELS) || (period_cycles == 0U) ||
	    (pulse_cycles > period_cycles)) {
		return -EINVAL;
	}

	if ((flags & ~PWM_POLARITY_INVERTED) != 0U) {
		return -ENOTSUP;
	}

	divider = DIV_ROUND_UP(period_cycles, AIC_PWM_PERIOD_MAX + 1U);
	if ((divider == 0U) || (divider > AIC_PWM_DIV_MAX)) {
		return -ERANGE;
	}

	period_count = DIV_ROUND_CLOSEST(period_cycles, divider);
	pulse_count = DIV_ROUND_CLOSEST(pulse_cycles, divider);
	period_count = CLAMP(period_count, 1U, AIC_PWM_PERIOD_MAX + 1U);
	pulse_count = MIN(pulse_count, period_count);

	if ((flags & PWM_POLARITY_INVERTED) != 0U) {
		action = AIC_PWM_ACTION_LOW |
			 (AIC_PWM_ACTION_HIGH << AIC_PWM_ACTION_CAU_SHIFT);
	} else {
		action = AIC_PWM_ACTION_HIGH |
			 (AIC_PWM_ACTION_LOW << AIC_PWM_ACTION_CAU_SHIFT);
	}

	if (pulse_cycles == 0U) {
		action = (flags & PWM_POLARITY_INVERTED) != 0U ? AIC_PWM_ACTION_DEFAULT : 0U;
	} else if (pulse_cycles == period_cycles) {
		action = (flags & PWM_POLARITY_INVERTED) != 0U ? 0U : AIC_PWM_ACTION_DEFAULT;
	}

	K_SPINLOCK(&data->lock) {
		value = sys_read32(artinchip_pwm_channel_reg(config, channel, AIC_PWM_TBCTL));
		value &= ~(AIC_PWM_TBCTL_DIV_MASK | AIC_PWM_TBCTL_MODE_MASK);
		value |= FIELD_PREP(AIC_PWM_TBCTL_DIV_MASK, divider - 1U);
		value |= AIC_PWM_TBCTL_MODE_UP;
		sys_write32(value, artinchip_pwm_channel_reg(config, channel, AIC_PWM_TBCTL));

		sys_write32(period_count - 1U,
			    artinchip_pwm_channel_reg(config, channel, AIC_PWM_TBPRD));
		sys_write32(pulse_count == period_count ? period_count : pulse_count,
			    artinchip_pwm_channel_reg(config, channel, AIC_PWM_CMPA));
		sys_write32(action, artinchip_pwm_channel_reg(config, channel, AIC_PWM_AQCTLA));

		value = sys_read32(config->base + AIC_PWM_CKCTL);
		sys_write32(value | BIT(channel), config->base + AIC_PWM_CKCTL);
		value = sys_read32(config->base + AIC_PWM_MCTL);
		sys_write32(value | BIT(channel), config->base + AIC_PWM_MCTL);
	}

	return 0;
}

static int artinchip_pwm_get_cycles_per_sec(const struct device *dev, uint32_t channel,
				       uint64_t *cycles)
{
	struct artinchip_pwm_data *data = dev->data;

	if (channel >= AIC_PWM_CHANNELS) {
		return -EINVAL;
	}

	*cycles = data->input_clock;

	return 0;
}

static int artinchip_pwm_init(const struct device *dev)
{
	const struct artinchip_pwm_config *config = dev->config;
	struct artinchip_pwm_data *data = dev->data;
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

	sys_write32(AIC_PWM_CTL_ENABLE, config->base + AIC_PWM_CTL);
	for (uint32_t channel = 0U; channel < AIC_PWM_CHANNELS; channel++) {
		sys_write32(AIC_PWM_CMP_SHADOW_ZRO_PRD,
			    artinchip_pwm_channel_reg(config, channel, AIC_PWM_CMPCTL));
	}

	return 0;
}

static DEVICE_API(pwm, artinchip_pwm_api) = {
	.set_cycles = artinchip_pwm_set_cycles,
	.get_cycles_per_sec = artinchip_pwm_get_cycles_per_sec,
};

PINCTRL_DT_INST_DEFINE(0);

static const struct artinchip_pwm_config artinchip_pwm_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
	.pinctrl = PINCTRL_DT_INST_DEV_CONFIG_GET(0),
};

static struct artinchip_pwm_data artinchip_pwm_data;

DEVICE_DT_INST_DEFINE(0, artinchip_pwm_init, NULL, &artinchip_pwm_data, &artinchip_pwm_config, POST_KERNEL,
		      CONFIG_PWM_INIT_PRIORITY, &artinchip_pwm_api);
