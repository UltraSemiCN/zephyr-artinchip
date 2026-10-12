/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_VE_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_VE_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#include <stdbool.h>
#include <stdint.h>

#define ARTINCHIP_VE_UNIT_AVC  BIT(0)
#define ARTINCHIP_VE_UNIT_JPEG BIT(1)
#define ARTINCHIP_VE_UNIT_PNG  BIT(2)

int artinchip_ve_reset(const struct device *dev);
void artinchip_ve_set_irq_enabled(const struct device *dev, bool enabled);
uint32_t artinchip_ve_get_active_units(const struct device *dev);
uint32_t artinchip_ve_get_jpeg_status(const struct device *dev);
uint32_t artinchip_ve_get_png_status(const struct device *dev);

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_VE_H_ */
