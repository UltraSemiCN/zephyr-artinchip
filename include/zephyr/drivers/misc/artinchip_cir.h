/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_CIR_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_CIR_H_

#include <zephyr/device.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*artinchip_cir_callback_t)(const struct device *dev, void *user_data);

int artinchip_cir_set_callback(const struct device *dev, artinchip_cir_callback_t callback,
			       void *user_data);
int artinchip_cir_read(const struct device *dev, uint8_t *pulses, size_t size);
bool artinchip_cir_is_busy(const struct device *dev);

#endif
