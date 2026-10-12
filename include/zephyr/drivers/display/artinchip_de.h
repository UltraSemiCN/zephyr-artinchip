/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DISPLAY_ARTINCHIP_DE_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISPLAY_ARTINCHIP_DE_H_

#include <zephyr/device.h>
#include <zephyr/drivers/mipi_dsi.h>

/** Update DE active size and porch timings before artinchip_de_bringup(). */
int artinchip_de_set_timings(const struct device *dev,
			     const struct mipi_dsi_timings *timings);

/**
 * @brief Start the ArtInChip display engine after DSI panel bring-up.
 *
 * Powers the DE, programs timing/UI registers, optionally fills the framebuffer,
 * and enables timing output.
 *
 * @param dev DE device.
 * @param fill_rgb565 RGB565 fill color, or 0 to clear the framebuffer.
 * @retval 0 on success.
 * @retval negative errno on failure.
 */
int artinchip_de_bringup(const struct device *dev, uint16_t fill_rgb565);

/** Fill the active framebuffer with eight RGB565 vertical color bars. */
int artinchip_de_test_pattern(const struct device *dev);

#endif /* ZEPHYR_INCLUDE_DRIVERS_DISPLAY_ARTINCHIP_DE_H_ */
