/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_MTOP_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_MTOP_H_
#include <zephyr/device.h>
#include <stdint.h>
struct artinchip_mtop_count {
	uint32_t write;
	uint32_t read;
};
int artinchip_mtop_start(const struct device *dev, uint32_t period);
void artinchip_mtop_stop(const struct device *dev);
int artinchip_mtop_get_count(const struct device *dev, uint8_t group, uint8_t port,
			     struct artinchip_mtop_count *count);
#endif
