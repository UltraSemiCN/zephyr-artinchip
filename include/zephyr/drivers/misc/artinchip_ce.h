/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_CE_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_CE_H_

#include <zephyr/device.h>
#include <stdbool.h>
#include <stdint.h>

uint32_t artinchip_ce_get_version(const struct device *dev);
uint32_t artinchip_ce_get_status(const struct device *dev);
uint32_t artinchip_ce_get_errors(const struct device *dev);
bool artinchip_ce_is_idle(const struct device *dev);
int artinchip_ce_reset(const struct device *dev);

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_CE_H_ */
