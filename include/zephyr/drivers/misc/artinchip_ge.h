/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_GE_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_GE_H_
#include <zephyr/device.h>
#include <stdbool.h>
#include <stdint.h>
uint32_t artinchip_ge_get_version(const struct device *dev);
uint32_t artinchip_ge_get_status(const struct device *dev);
bool artinchip_ge_is_busy(const struct device *dev);
int artinchip_ge_reset(const struct device *dev);
#endif
