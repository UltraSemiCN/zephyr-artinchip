/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_AXICFG_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_AXICFG_H_

#include <zephyr/device.h>

#include <stdbool.h>
#include <stdint.h>

enum artinchip_axicfg_port {
	ARTINCHIP_AXICFG_PORT_CPU,
	ARTINCHIP_AXICFG_PORT_AHB,
	ARTINCHIP_AXICFG_PORT_DE,
	ARTINCHIP_AXICFG_PORT_GE,
	ARTINCHIP_AXICFG_PORT_VE,
	ARTINCHIP_AXICFG_PORT_DVP_MDI,
	ARTINCHIP_AXICFG_PORT_CE,
	ARTINCHIP_AXICFG_PORT_COUNT,
};

enum artinchip_axicfg_direction {
	ARTINCHIP_AXICFG_READ,
	ARTINCHIP_AXICFG_WRITE,
};

struct artinchip_axicfg_config {
	bool regulator_enable;
	bool slave_ready;
	uint8_t burst_limit;
	bool internal_qos;
	uint8_t qos;
	uint16_t basic_rate;
	uint16_t burst_rate;
};

int artinchip_axicfg_configure(const struct device *dev, enum artinchip_axicfg_port port,
			       enum artinchip_axicfg_direction direction,
			       const struct artinchip_axicfg_config *config);
int artinchip_axicfg_get_config(const struct device *dev, enum artinchip_axicfg_port port,
				enum artinchip_axicfg_direction direction,
				struct artinchip_axicfg_config *config);
int artinchip_axicfg_set_priority(const struct device *dev, enum artinchip_axicfg_port port,
				  uint8_t priority);

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_ARTINCHIP_AXICFG_H_ */
