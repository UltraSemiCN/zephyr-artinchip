/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_SPIENC_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_SPIENC_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

struct artinchip_spienc_config {
	uint8_t controller;
	uint32_t address;
	uint32_t cipher_position;
	uint32_t cipher_length;
	uint32_t tweak;
	bool hardware_tweak;
	bool bypass;
};

int artinchip_spienc_configure(const struct device *dev,
			       const struct artinchip_spienc_config *config);
int artinchip_spienc_start(const struct device *dev);
int artinchip_spienc_stop(const struct device *dev);
int artinchip_spienc_set_xip(const struct device *dev, bool enable);
int artinchip_spienc_is_empty(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_SPIENC_H_ */
