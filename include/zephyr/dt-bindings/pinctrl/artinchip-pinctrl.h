/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_ARTINCHIP_PINCTRL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_ARTINCHIP_PINCTRL_H_

#define ARTINCHIP_PINMUX_PORT_SHIFT     24U
#define ARTINCHIP_PINMUX_PIN_SHIFT      16U
#define ARTINCHIP_PINMUX_FUNC_SHIFT     8U
#define ARTINCHIP_PINMUX_PULL_SHIFT     4U
#define ARTINCHIP_PINMUX_STRENGTH_SHIFT 0U

#define ARTINCHIP_PINMUX_PORT_MASK     0xFFU
#define ARTINCHIP_PINMUX_PIN_MASK      0xFFU
#define ARTINCHIP_PINMUX_FUNC_MASK     0xFFU
#define ARTINCHIP_PINMUX_PULL_MASK     0xFU
#define ARTINCHIP_PINMUX_STRENGTH_MASK 0xFU

#define ARTINCHIP_PULL_DISABLE  0U
#define ARTINCHIP_PULL_DOWN     2U
#define ARTINCHIP_PULL_UP       3U
#define ARTINCHIP_DRIVE_DEFAULT 3U

/*
 * Match the luban-lite FDT pinmux encoding:
 *   [31:24] GPIO group, [23:16] pin, [15:8] function,
 *   [7:4] pull setting, [3:0] drive strength.
 */
#define ARTINCHIP_PINMUX_FULL(port, pin, func, pull, strength)                                     \
	((((port) & ARTINCHIP_PINMUX_PORT_MASK) << ARTINCHIP_PINMUX_PORT_SHIFT) |                 \
	 (((pin) & ARTINCHIP_PINMUX_PIN_MASK) << ARTINCHIP_PINMUX_PIN_SHIFT) |                    \
	 (((func) & ARTINCHIP_PINMUX_FUNC_MASK) << ARTINCHIP_PINMUX_FUNC_SHIFT) |                  \
	 (((pull) & ARTINCHIP_PINMUX_PULL_MASK) << ARTINCHIP_PINMUX_PULL_SHIFT) |                  \
	 (((strength) & ARTINCHIP_PINMUX_STRENGTH_MASK) << ARTINCHIP_PINMUX_STRENGTH_SHIFT))

#define ARTINCHIP_PINMUX(port, pin, func)                                                          \
	ARTINCHIP_PINMUX_FULL(port, pin, func, ARTINCHIP_PULL_DISABLE, ARTINCHIP_DRIVE_DEFAULT)

#define ARTINCHIP_PINMUX_UP(port, pin, func)                                                       \
	ARTINCHIP_PINMUX_FULL(port, pin, func, ARTINCHIP_PULL_UP, ARTINCHIP_DRIVE_DEFAULT)

#define ARTINCHIP_PINMUX_DOWN(port, pin, func)                                                      \
	ARTINCHIP_PINMUX_FULL(port, pin, func, ARTINCHIP_PULL_DOWN, ARTINCHIP_DRIVE_DEFAULT)

#define ARTINCHIP_PINMUX_PORT(value)                                                               \
	(((value) >> ARTINCHIP_PINMUX_PORT_SHIFT) & ARTINCHIP_PINMUX_PORT_MASK)
#define ARTINCHIP_PINMUX_PIN(value)                                                                \
	(((value) >> ARTINCHIP_PINMUX_PIN_SHIFT) & ARTINCHIP_PINMUX_PIN_MASK)
#define ARTINCHIP_PINMUX_FUNC(value)                                                               \
	(((value) >> ARTINCHIP_PINMUX_FUNC_SHIFT) & ARTINCHIP_PINMUX_FUNC_MASK)
#define ARTINCHIP_PINMUX_PULL(value)                                                               \
	(((value) >> ARTINCHIP_PINMUX_PULL_SHIFT) & ARTINCHIP_PINMUX_PULL_MASK)
#define ARTINCHIP_PINMUX_STRENGTH(value)                                                           \
	(((value) >> ARTINCHIP_PINMUX_STRENGTH_SHIFT) & ARTINCHIP_PINMUX_STRENGTH_MASK)

#define ARTINCHIP_PORT_A 0U
#define ARTINCHIP_PORT_B 1U
#define ARTINCHIP_PORT_C 2U
#define ARTINCHIP_PORT_D 3U
#define ARTINCHIP_PORT_E 4U

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_ARTINCHIP_PINCTRL_H_ */
