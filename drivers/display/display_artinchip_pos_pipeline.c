/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_pos_display_pipeline

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/artinchip_de.h>
#include <zephyr/drivers/display/artinchip_pos_panel.h>
#include <zephyr/drivers/mipi_dsi/artinchip_dsi.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(display_artinchip_pos_pipeline, CONFIG_DISPLAY_LOG_LEVEL);

struct artinchip_pos_pipeline_config {
	const struct device *dsi;
	const struct device *panel;
	const struct device *de;
};

static int artinchip_pos_pipeline_init(const struct device *dev)
{
	const struct artinchip_pos_pipeline_config *config = dev->config;
	int ret;

	if (!device_is_ready(config->dsi) || !device_is_ready(config->panel) ||
	    !device_is_ready(config->de)) {
		return -ENODEV;
	}

	LOG_DBG("starting panel attach");
	ret = artinchip_pos_panel_attach_dsi(config->panel);
	if (ret != 0) {
		LOG_ERR("panel attach failed: %d", ret);
		return ret;
	}

	LOG_DBG("starting DSI prepare");
	ret = artinchip_dsi_prepare(config->dsi);
	if (ret != 0) {
		LOG_ERR("DSI host prepare failed: %d", ret);
		return ret;
	}

	LOG_DBG("starting panel bringup");
	ret = artinchip_pos_panel_bringup(config->panel);
	if (ret != 0) {
		LOG_ERR("panel bring-up failed: %d", ret);
		return ret;
	}

	LOG_DBG("starting DE bringup");
	ret = artinchip_de_bringup(config->de, 0x0000U);
	if (ret != 0) {
		LOG_ERR("DE bring-up failed: %d", ret);
		return ret;
	}

	if (IS_ENABLED(CONFIG_DISPLAY_ARTINCHIP_POS_PIPELINE_TEST_PATTERN)) {
		ret = artinchip_de_test_pattern(config->de);
		if (ret != 0) {
			LOG_ERR("DE test pattern failed: %d", ret);
			return ret;
		}
	}

	LOG_DBG("starting backlight");
	ret = artinchip_pos_panel_backlight_enable(config->panel);
	if (ret != 0) {
		LOG_ERR("panel backlight enable failed: %d", ret);
		return ret;
	}

	/*
	 * Stop DE scan-out until the first LVGL frame is in the FB.
	 * Do not toggle panel DCS / DSI videomode here: that path is racy on
	 * this PHY (LP-stop timeouts, R/B swap, occasional black screen).
	 */
	if (!IS_ENABLED(CONFIG_DISPLAY_ARTINCHIP_POS_PIPELINE_TEST_PATTERN)) {
		ret = display_blanking_on(config->de);
		if (ret != 0) {
			LOG_WRN("DE blanking_on failed: %d (continuing)", ret);
		}
	}

	LOG_INF("POS display pipeline ready");
	return 0;
}

#define ARTINCHIP_POS_PIPELINE_DEFINE(inst)                                      \
	static const struct artinchip_pos_pipeline_config pipeline_config_##inst = { \
		.dsi = DEVICE_DT_GET(DT_INST_PHANDLE(inst, dsi_host)),              \
		.panel = DEVICE_DT_GET(DT_INST_PHANDLE(inst, panel)),                \
		.de = DEVICE_DT_GET(DT_INST_PHANDLE(inst, display_engine)),          \
	};                                                                            \
	DEVICE_DT_INST_DEFINE(inst, artinchip_pos_pipeline_init, NULL, NULL,           \
			      &pipeline_config_##inst, POST_KERNEL,                    \
			      CONFIG_DISPLAY_ARTINCHIP_POS_PIPELINE_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(ARTINCHIP_POS_PIPELINE_DEFINE)
