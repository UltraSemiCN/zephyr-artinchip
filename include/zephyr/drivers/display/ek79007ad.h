/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DISPLAY_EK79007AD_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISPLAY_EK79007AD_H_

#include <zephyr/device.h>

/**
 * @brief Attach the EK79007AD panel to the DSI host.
 */
int ek79007ad_attach_dsi(const struct device *dev);

/**
 * @brief Complete deferred EK79007AD panel bring-up.
 *
 * Sends the DCS init sequence in command mode, switches back to video mode,
 * and enables the backlight GPIO. Call ek79007ad_attach_dsi() first.
 *
 * @param dev EK79007AD panel device.
 * @retval 0 on success.
 * @retval negative errno on failure.
 */
int ek79007ad_bringup(const struct device *dev);

/**
 * @brief Enable the panel backlight GPIO (luban-lite panel_backlight_enable).
 */
int ek79007ad_backlight_enable(const struct device *dev);

#endif /* ZEPHYR_INCLUDE_DRIVERS_DISPLAY_EK79007AD_H_ */
