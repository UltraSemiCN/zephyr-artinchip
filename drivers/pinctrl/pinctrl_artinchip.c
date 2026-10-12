/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_pinctrl

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/dt-bindings/pinctrl/artinchip-pinctrl.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_PINCTRL_BASE         (DT_REG_ADDR(DT_INST(0, artinchip_pinctrl)) - 0x80U)
#define AIC_PINCTRL_GROUP_STRIDE 0x100U
#define AIC_PINCTRL_CFG_BASE     0x80U
#define AIC_PINCTRL_CFG_STRIDE   0x4U

#define AIC_PINCTRL_FUNC_MASK  GENMASK(3, 0)
#define AIC_PINCTRL_DRIVE_MASK GENMASK(6, 4)
#define AIC_PINCTRL_PULL_MASK  GENMASK(9, 8)

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	ARG_UNUSED(reg);

#if DT_INST_NODE_HAS_PROP(0, clocks)
	const struct device *clock = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0));
	int ret;

	if (!device_is_ready(clock)) {
		return -ENODEV;
	}
	ret = clock_control_on(clock, (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, clkid));
	if (ret != 0) {
		return ret;
	}
#endif
#if DT_INST_NODE_HAS_PROP(0, resets)
	const struct reset_dt_spec reset = RESET_DT_SPEC_INST_GET(0);
	int reset_ret;

	if (!device_is_ready(reset.dev)) {
		return -ENODEV;
	}
	reset_ret = reset_line_deassert_dt(&reset);
	if (reset_ret != 0) {
		return reset_ret;
	}
#endif

	for (uint8_t i = 0U; i < pin_cnt; i++) {
		uint32_t port = ARTINCHIP_PINMUX_PORT(pins[i].pinmux);
		uint32_t pin = ARTINCHIP_PINMUX_PIN(pins[i].pinmux);
		uintptr_t cfg_reg = AIC_PINCTRL_BASE + port * AIC_PINCTRL_GROUP_STRIDE +
				    AIC_PINCTRL_CFG_BASE + pin * AIC_PINCTRL_CFG_STRIDE;
		uint32_t value;

		if ((port > ARTINCHIP_PORT_E) || (pin >= 32U) ||
		    (ARTINCHIP_PINMUX_FUNC(pins[i].pinmux) > 15U) ||
		    (pins[i].drive_strength > 7U)) {
			return -EINVAL;
		}

		value = sys_read32(cfg_reg);
		value &= ~(AIC_PINCTRL_FUNC_MASK | AIC_PINCTRL_DRIVE_MASK |
			   AIC_PINCTRL_PULL_MASK);
		value |= FIELD_PREP(AIC_PINCTRL_FUNC_MASK,
				    ARTINCHIP_PINMUX_FUNC(pins[i].pinmux));
		value |= FIELD_PREP(AIC_PINCTRL_DRIVE_MASK, pins[i].drive_strength);
		value |= FIELD_PREP(AIC_PINCTRL_PULL_MASK, pins[i].pull);
		sys_write32(value, cfg_reg);
	}

	return 0;
}
