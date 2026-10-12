/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_ADCIM_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_ADCIM_H_

#include <zephyr/device.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int artinchip_adcim_get_calibration(const struct device *dev, uint16_t *calibration);
int artinchip_adcim_calibrate_value(const struct device *dev, uint16_t raw, uint16_t *calibrated);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_ADCIM_H_ */
