/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_RISCV_ARTINCHIP_PINCTRL_SOC_H_
#define ZEPHYR_SOC_RISCV_ARTINCHIP_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/types.h>

struct pinctrl_soc_pin {
	uint32_t pinmux;
	uint8_t drive_strength;
	uint8_t pull;
};

typedef struct pinctrl_soc_pin pinctrl_soc_pin_t;

#define ARTINCHIP_D13X_PULL_NONE 0U
#define ARTINCHIP_D13X_PULL_DOWN 2U
#define ARTINCHIP_D13X_PULL_UP   3U

#define Z_PINCTRL_ARTINCHIP_PULL(node_id)                                                         \
	COND_CODE_1(DT_PROP(node_id, bias_pull_up), (ARTINCHIP_D13X_PULL_UP),                     \
		    (COND_CODE_1(DT_PROP(node_id, bias_pull_down),                                \
				 (ARTINCHIP_D13X_PULL_DOWN), (ARTINCHIP_D13X_PULL_NONE))))

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                                               \
	{                                                                                          \
		.pinmux = DT_PROP_BY_IDX(node_id, prop, idx),                                      \
		.drive_strength = DT_PROP_OR(node_id, artinchip_drive_strength, 3),                 \
		.pull = Z_PINCTRL_ARTINCHIP_PULL(node_id),                                         \
	},

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                                   \
	{                                                                                          \
		DT_FOREACH_CHILD_VARGS(DT_PHANDLE(node_id, prop), DT_FOREACH_PROP_ELEM, pinmux,    \
				      Z_PINCTRL_STATE_PIN_INIT)                                     \
	}

#endif /* ZEPHYR_SOC_RISCV_ARTINCHIP_PINCTRL_SOC_H_ */
