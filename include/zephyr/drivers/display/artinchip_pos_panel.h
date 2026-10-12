/* SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd. */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DISPLAY_ARTINCHIP_POS_PANEL_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISPLAY_ARTINCHIP_POS_PANEL_H_

#include <zephyr/device.h>

int artinchip_pos_panel_attach_dsi(const struct device *dev);
int artinchip_pos_panel_bringup(const struct device *dev);
int artinchip_pos_panel_backlight_enable(const struct device *dev);

#endif
