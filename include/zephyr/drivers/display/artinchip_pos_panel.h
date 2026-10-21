/* SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd. */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DISPLAY_ARTINCHIP_POS_PANEL_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISPLAY_ARTINCHIP_POS_PANEL_H_

#include <stdbool.h>

#include <zephyr/device.h>

int artinchip_pos_panel_attach_dsi(const struct device *dev);
int artinchip_pos_panel_bringup(const struct device *dev);
int artinchip_pos_panel_backlight_enable(const struct device *dev);

/**
 * @brief Panel DCS display on/off only (does not touch PF0 backlight/power GPIO).
 *
 * Switches DSI to command mode around the DCS write, then restores video mode.
 * Use after bring-up to hide the progressive first LVGL paint, then turn on
 * once the framebuffer already holds a complete frame.
 */
int artinchip_pos_panel_set_display(const struct device *dev, bool on);

#endif
