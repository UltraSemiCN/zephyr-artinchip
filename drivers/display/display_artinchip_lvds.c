/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_lvds

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/display/artinchip_de.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>

LOG_MODULE_REGISTER(display_artinchip_lvds, CONFIG_DISPLAY_LOG_LEVEL);

#define LVDS_CTL             0x00U
#define LVDS_LINK0_SWAP       0x20U
#define LVDS_LINK0_POLARITY   0x28U
#define LVDS_LINK0_PHY        0x30U
#define LVDS_ENABLE          BIT(0)
#define LVDS_SYNC_MODE       BIT(1)
#define LVDS_LINK_MASK       GENMASK(5, 4)
#define LVDS_MAPPING_MASK    GENMASK(9, 8)
/* PD18/19=D0, PD20/21=D1, PD22/23=D2, PD24/25=CK, PD26/27=D3. */
#define LVDS_LINK0_LANES      0x43210U
/* Bias/LDO enabled, 1.43 V common mode, 1.2 mA slope, 350 mV differential. */
#define LVDS_PHY_CONFIG       0xfaU

struct artinchip_lvds_config {
	uintptr_t base;
	const struct device *clock;
	clock_control_subsys_t clock_id;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pinctrl;
	const struct device *de;
	struct gpio_dt_spec enable;
	uint8_t mapping;
	uint8_t link;
};

static int artinchip_lvds_init(const struct device *dev)
{
	const struct artinchip_lvds_config *config = dev->config;
	int ret;

	if (!device_is_ready(config->clock) || !device_is_ready(config->reset.dev) ||
	    !device_is_ready(config->de) || !gpio_is_ready_dt(&config->enable)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->enable, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		return ret;
	}
	ret = pinctrl_apply_state(config->pinctrl, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}
	ret = clock_control_on(config->clock, config->clock_id);
	if (ret != 0) {
		LOG_ERR("LVDS clock setup failed: %d", ret);
		return ret;
	}
	ret = reset_line_toggle_dt(&config->reset);
	if (ret != 0) {
		goto clock_off;
	}

	sys_write32(LVDS_LINK0_LANES, config->base + LVDS_LINK0_SWAP + config->link * 4U);
	sys_write32(0U, config->base + LVDS_LINK0_POLARITY + config->link * 4U);
	sys_write32(LVDS_PHY_CONFIG, config->base + LVDS_LINK0_PHY + config->link * 4U);
	sys_write32(FIELD_PREP(LVDS_MAPPING_MASK, config->mapping) |
		    FIELD_PREP(LVDS_LINK_MASK, config->link) |
		    LVDS_SYNC_MODE | LVDS_ENABLE, config->base + LVDS_CTL);

	ret = artinchip_de_bringup(config->de, 0U);
	if (ret == 0) {
		ret = gpio_pin_set_dt(&config->enable, 1);
	}
	if (ret == 0) {
		return 0;
	}

	sys_write32(0U, config->base + LVDS_CTL);
	(void)reset_line_assert_dt(&config->reset);
clock_off:
	(void)clock_control_off(config->clock, config->clock_id);
	LOG_ERR("LVDS initialization failed: %d", ret);
	return ret;
}

#define ARTINCHIP_LVDS_DEFINE(inst)                                                        \
	PINCTRL_DT_INST_DEFINE(inst);                                                       \
	static const struct artinchip_lvds_config lvds_config_##inst = {                    \
		.base = DT_INST_REG_ADDR(inst),                                             \
		.clock = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                            \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),         \
		.reset = RESET_DT_SPEC_INST_GET(inst),                                      \
		.pinctrl = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                             \
		.de = DEVICE_DT_GET(DT_INST_PHANDLE(inst, display_engine)),                  \
		.enable = GPIO_DT_SPEC_INST_GET(inst, enable_gpios),                        \
		.link = DT_INST_PROP(inst, link),                                          \
		.mapping = DT_INST_ENUM_IDX(inst, data_mapping),                            \
	};                                                                                 \
	DEVICE_DT_INST_DEFINE(inst, artinchip_lvds_init, NULL, NULL, &lvds_config_##inst,    \
			      POST_KERNEL, CONFIG_DISPLAY_ARTINCHIP_LVDS_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(ARTINCHIP_LVDS_DEFINE)
