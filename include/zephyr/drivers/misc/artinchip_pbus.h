/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_PBUS_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_PBUS_H_
#include <zephyr/device.h>
#include <stddef.h>
#include <stdint.h>
int artinchip_pbus_configure(const struct device *dev, uint32_t cfg0, uint32_t cfg1,
			     uint32_t cfg2);
int artinchip_pbus_read(const struct device *dev, uint32_t offset, void *buffer, size_t length);
int artinchip_pbus_write(const struct device *dev, uint32_t offset, const void *buffer,
			  size_t length);
#endif
