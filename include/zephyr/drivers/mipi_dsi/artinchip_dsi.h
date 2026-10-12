/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_ARTINCHIP_DSI_H_
#define ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_ARTINCHIP_DSI_H_

#include <zephyr/device.h>
#include <zephyr/drivers/mipi_dsi.h>

/**
 * @brief Switch the ArtInChip DSI host between command and video mode.
 *
 * Command mode must be active before panel DCS initialization. Video mode
 * must be restored before the display engine starts streaming pixels.
 *
 * @param dev MIPI DSI host device.
 * @param video_enable true to enter video mode, false for command mode.
 * @retval 0 on success.
 * @retval negative errno on failure.
 */
int artinchip_dsi_set_videomode(const struct device *dev, bool video_enable);

/**
 * @brief Update video timings used when leaving command mode.
 *
 * @param dev MIPI DSI host device.
 * @param timings Panel video timings selected for the detected ID.
 * @retval 0 on success.
 * @retval negative errno on failure.
 */
int artinchip_dsi_update_timings(const struct device *dev,
				 const struct mipi_dsi_timings *timings);

/**
 * @brief Power and prepare the ArtInChip DSI host and PHY.
 *
 * This host-only operation does not attach or initialize a panel and does not
 * start a display engine.
 *
 * @param dev MIPI DSI host device.
 * @retval 0 on success.
 * @retval negative errno on failure.
 */
int artinchip_dsi_prepare(const struct device *dev);

#endif /* ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_ARTINCHIP_DSI_H_ */
