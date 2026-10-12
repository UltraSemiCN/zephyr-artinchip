/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT artinchip_d13x_pwmcs
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_PWMCS_OUTPUTS      24U
#define AIC_PWMCS_UNITS        12U
#define AIC_PWMCS_UNIT(ch)     ((ch) / 2U)
#define AIC_PWMCS_REG(ch, reg) (AIC_PWMCS_UNIT(ch) * 0x100U + (reg))
#define AIC_PWMCS_PRD          0x000U
#define AIC_PWMCS_CONF         0x00CU
#define AIC_PWMCS_CMPA         0x014U
#define AIC_PWMCS_CMPB         0x018U
#define AIC_PWMCS_ACTA         0x020U
#define AIC_PWMCS_ACTB         0x024U
#define AIC_PWMCS_GLB_CLK      0xF020U
#define AIC_PWMCS_GLB_EN       0xF014U
#define AIC_PWMCS_MODE         GENMASK(1, 0)
#define AIC_PWMCS_DIV1         GENMASK(9, 7)
#define AIC_PWMCS_DIV2         GENMASK(12, 10)
#define AIC_PWMCS_MODE_STOP    3U
#define AIC_PWMCS_ACTION_HIGH  2U
#define AIC_PWMCS_ACTION_LOW   1U
#define AIC_PWMCS_PERIOD_MAX   UINT16_MAX

struct artinchip_pwmcs_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
};
struct artinchip_pwmcs_data {
	struct k_spinlock lock;
	uint32_t rate;
};

static int artinchip_pwmcs_select_divider(uint32_t period, uint32_t *divider, uint32_t *encoding)
{
	static const uint16_t div1[] = {1U, 2U, 4U, 6U, 8U, 10U, 12U, 14U};
	static const uint16_t div2[] = {1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U};

	for (uint32_t i = 0U; i < ARRAY_SIZE(div1); i++) {
		for (uint32_t j = 0U; j < ARRAY_SIZE(div2); j++) {
			uint32_t value = div1[i] * div2[j];

			if (DIV_ROUND_UP(period, value) <= AIC_PWMCS_PERIOD_MAX + 1U) {
				*divider = value;
				*encoding = FIELD_PREP(AIC_PWMCS_DIV1, i) |
					    FIELD_PREP(AIC_PWMCS_DIV2, j);
				return 0;
			}
		}
	}
	return -ERANGE;
}

static int artinchip_pwmcs_set_cycles(const struct device *dev, uint32_t channel,
				  uint32_t period_cycles, uint32_t pulse_cycles,
				  pwm_flags_t flags)
{
	const struct artinchip_pwmcs_config *config = dev->config;
	struct artinchip_pwmcs_data *data = dev->data;
	uint32_t divider;
	uint32_t encoding;
	uint32_t period;
	uint32_t pulse;
	uint32_t action;
	uint32_t unit;
	int ret;

	if ((channel >= AIC_PWMCS_OUTPUTS) || (period_cycles == 0U) ||
	    (pulse_cycles > period_cycles) || ((flags & ~PWM_POLARITY_INVERTED) != 0U)) {
		return -EINVAL;
	}
	ret = artinchip_pwmcs_select_divider(period_cycles, &divider, &encoding);
	if (ret != 0) {
		return ret;
	}
	unit = AIC_PWMCS_UNIT(channel);
	period = DIV_ROUND_CLOSEST(period_cycles, divider);
	pulse = DIV_ROUND_CLOSEST(pulse_cycles, divider);
	action = AIC_PWMCS_ACTION_HIGH |
		 (AIC_PWMCS_ACTION_LOW << ((channel & 1U) == 0U ? 4U : 8U));
	if ((flags & PWM_POLARITY_INVERTED) != 0U) {
		action = AIC_PWMCS_ACTION_LOW |
			 (AIC_PWMCS_ACTION_HIGH << ((channel & 1U) == 0U ? 4U : 8U));
	}

	K_SPINLOCK(&data->lock) {
		sys_write32(period - 1U, config->base + AIC_PWMCS_REG(channel, AIC_PWMCS_PRD));
		sys_write32(pulse, config->base + AIC_PWMCS_REG(channel,
			    (channel & 1U) == 0U ? AIC_PWMCS_CMPA : AIC_PWMCS_CMPB));
		sys_write32(action, config->base + AIC_PWMCS_REG(channel,
			    (channel & 1U) == 0U ? AIC_PWMCS_ACTA : AIC_PWMCS_ACTB));
		sys_write32(encoding, config->base + AIC_PWMCS_REG(channel, AIC_PWMCS_CONF));
		sys_write32(sys_read32(config->base + AIC_PWMCS_GLB_CLK) | BIT(unit),
			    config->base + AIC_PWMCS_GLB_CLK);
	}
	return 0;
}

static int artinchip_pwmcs_get_cycles_per_sec(const struct device *dev, uint32_t channel,
					  uint64_t *cycles)
{
	struct artinchip_pwmcs_data *data = dev->data;

	if (channel >= AIC_PWMCS_OUTPUTS) {
		return -EINVAL;
	}
	*cycles = data->rate;
	return 0;
}

static int artinchip_pwmcs_init(const struct device *dev)
{
	const struct artinchip_pwmcs_config *config = dev->config;
	struct artinchip_pwmcs_data *data = dev->data;
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
	ret = clock_control_get_rate(config->clock_dev, config->clock_subsys, &data->rate);
	if (ret != 0) {
		return ret;
	}
	sys_write32(1U, config->base + AIC_PWMCS_GLB_EN);
	for (uint32_t unit = 0U; unit < AIC_PWMCS_UNITS; unit++) {
		sys_write32(AIC_PWMCS_MODE_STOP, config->base + unit * 0x100U + AIC_PWMCS_CONF);
	}
	return 0;
}

static DEVICE_API(pwm, artinchip_pwmcs_api) = {
	.set_cycles = artinchip_pwmcs_set_cycles,
	.get_cycles_per_sec = artinchip_pwmcs_get_cycles_per_sec,
};
static const struct artinchip_pwmcs_config artinchip_pwmcs_config = {
	.base = DT_INST_REG_ADDR(0),
	.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid),
	.reset = RESET_DT_SPEC_INST_GET(0),
};
static struct artinchip_pwmcs_data artinchip_pwmcs_data;
DEVICE_DT_INST_DEFINE(0, artinchip_pwmcs_init, NULL, &artinchip_pwmcs_data, &artinchip_pwmcs_config,
		      POST_KERNEL, CONFIG_PWM_INIT_PRIORITY, &artinchip_pwmcs_api);
