/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_rgb

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/dt-bindings/clock/artinchip-clock.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>

#if defined(CONFIG_DISPLAY_ARTINCHIP_RGB_SHELL)
#include <zephyr/shell/shell.h>
#endif

LOG_MODULE_REGISTER(display_artinchip_rgb, CONFIG_DISPLAY_LOG_LEVEL);

#define RGB_LCD_CTL       0x00U
#define RGB_CLK_CTL       0x10U
#define RGB_DATA_SEL      0x20U
#define RGB_DATA_SEQ_SEL  0x30U
#define RGB_VERSION       0xfcU

#define RGB_LCD_CTL_EN              BIT(0)
#define RGB_LCD_CTL_MODE_MASK       GENMASK(5, 4)
#define RGB_LCD_CTL_MODE(x)         FIELD_PREP(RGB_LCD_CTL_MODE_MASK, (x))
#define RGB_LCD_CTL_PRGB_MODE_MASK  GENMASK(10, 8)
#define RGB_LCD_CTL_PRGB_MODE(x)    FIELD_PREP(RGB_LCD_CTL_PRGB_MODE_MASK, (x))
#define RGB_LCD_CTL_SRGB_MODE       BIT(12)
#define RGB_CLK_CTL_CKO_PHASE_MASK  GENMASK(1, 0)
#define RGB_CLK_CTL_CKO_PHASE(x)    FIELD_PREP(RGB_CLK_CTL_CKO_PHASE_MASK, (x))
#define RGB_DATA_OUT_SEL_MASK       GENMASK(2, 0)
#define RGB_DATA_OUT_SEL(x)         FIELD_PREP(RGB_DATA_OUT_SEL_MASK, (x))
#define RGB_DATA_OUT_SEL_VALID_MASK GENMASK(14, 4)
#define RGB_DATA_OUT_SEL_VALID(x)   FIELD_PREP(RGB_DATA_OUT_SEL_VALID_MASK, (x))

#define ARTINCHIP_RGB_MODE_PRGB 0U
#define ARTINCHIP_RGB_MODE_SRGB 1U
#define ARTINCHIP_RGB_PRGB_24BIT 0U
#define ARTINCHIP_RGB_SRGB_8BIT 0U

struct artinchip_rgb_config {
	uintptr_t base;
	uint32_t mode;
	uint32_t format;
	uint32_t data_order;
	uint32_t clock_phase;
	bool data_mirror;
	bool manual_start;
	const struct pinctrl_dev_config *pincfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_id;
	struct reset_dt_spec reset;
};

struct artinchip_rgb_data {
	bool initialized;
	bool enabled;
};

static uint32_t artinchip_rgb_read(const struct artinchip_rgb_config *config, uint32_t offset)
{
	return sys_read32(config->base + offset);
}

static void artinchip_rgb_write(const struct artinchip_rgb_config *config, uint32_t offset,
				uint32_t value)
{
	sys_write32(value, config->base + offset);
}

static void artinchip_rgb_update_bits(const struct artinchip_rgb_config *config,
				      uint32_t offset, uint32_t mask, uint32_t value)
{
	uint32_t reg = artinchip_rgb_read(config, offset);

	reg &= ~mask;
	reg |= value & mask;
	artinchip_rgb_write(config, offset, reg);
}

static uint32_t artinchip_rgb_bits_valid(uint32_t mode, uint32_t format)
{
	if (mode == ARTINCHIP_RGB_MODE_PRGB) {
		switch (format) {
		case 0U:
			return 0x777U;
		case 1U:
		case 2U:
			return 0x555U;
		case 3U:
		case 4U:
			return 0x454U;
		default:
			return 0U;
		}
	}

	if (mode == ARTINCHIP_RGB_MODE_SRGB) {
		return format == ARTINCHIP_RGB_SRGB_8BIT ? 0x777U : 0x555U;
	}

	return 0U;
}

static int artinchip_rgb_hw_start(const struct device *dev)
{
	const struct artinchip_rgb_config *config = dev->config;
	struct artinchip_rgb_data *data = dev->data;
	int ret;

	if (data->initialized) {
		return 0;
	}

	ret = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}

	ret = clock_control_on(config->clock_dev, config->clock_id);
	if (ret != 0) {
		return ret;
	}

	ret = reset_line_deassert_dt(&config->reset);
	if (ret != 0) {
		(void)clock_control_off(config->clock_dev, config->clock_id);
		return ret;
	}

	data->initialized = true;

	return 0;
}

static int artinchip_rgb_configure(const struct device *dev)
{
	const struct artinchip_rgb_config *config = dev->config;
	uint32_t valid_bits;

	if (config->mode > ARTINCHIP_RGB_MODE_SRGB) {
		return -EINVAL;
	}

	artinchip_rgb_update_bits(config, RGB_LCD_CTL, RGB_LCD_CTL_MODE_MASK,
				  RGB_LCD_CTL_MODE(config->mode));

	if (config->mode == ARTINCHIP_RGB_MODE_PRGB) {
		artinchip_rgb_update_bits(config, RGB_LCD_CTL, RGB_LCD_CTL_PRGB_MODE_MASK,
					  RGB_LCD_CTL_PRGB_MODE(config->format));
	} else if (config->format != ARTINCHIP_RGB_SRGB_8BIT) {
		artinchip_rgb_update_bits(config, RGB_LCD_CTL, RGB_LCD_CTL_SRGB_MODE,
					  RGB_LCD_CTL_SRGB_MODE);
	}

	if (config->data_mirror) {
		valid_bits = artinchip_rgb_bits_valid(config->mode, config->format);
		if (valid_bits == 0U) {
			return -EINVAL;
		}
		artinchip_rgb_update_bits(config, RGB_DATA_SEQ_SEL, RGB_DATA_OUT_SEL_MASK,
					  RGB_DATA_OUT_SEL(7U));
		artinchip_rgb_update_bits(config, RGB_DATA_SEQ_SEL, RGB_DATA_OUT_SEL_VALID_MASK,
					  RGB_DATA_OUT_SEL_VALID(valid_bits));
	}

	artinchip_rgb_write(config, RGB_DATA_SEL, config->data_order);
	artinchip_rgb_update_bits(config, RGB_CLK_CTL, RGB_CLK_CTL_CKO_PHASE_MASK,
				  RGB_CLK_CTL_CKO_PHASE(config->clock_phase));

	return 0;
}

static int artinchip_rgb_enable(const struct device *dev)
{
	const struct artinchip_rgb_config *config = dev->config;
	struct artinchip_rgb_data *data = dev->data;
	int ret;

	ret = artinchip_rgb_hw_start(dev);
	if (ret != 0) {
		return ret;
	}

	ret = artinchip_rgb_configure(dev);
	if (ret != 0) {
		return ret;
	}

	artinchip_rgb_update_bits(config, RGB_LCD_CTL, RGB_LCD_CTL_EN, RGB_LCD_CTL_EN);
	data->enabled = true;

	return 0;
}

static int artinchip_rgb_disable(const struct device *dev)
{
	const struct artinchip_rgb_config *config = dev->config;
	struct artinchip_rgb_data *data = dev->data;

	artinchip_rgb_update_bits(config, RGB_LCD_CTL, RGB_LCD_CTL_EN, 0U);
	data->enabled = false;

	return 0;
}

static int artinchip_rgb_init(const struct device *dev)
{
	const struct artinchip_rgb_config *config = dev->config;

	if (!device_is_ready(config->clock_dev) || !device_is_ready(config->reset.dev)) {
		return -ENODEV;
	}

	if (config->manual_start) {
		LOG_INF("RGB output start deferred to shell");
		return 0;
	}

	return artinchip_rgb_enable(dev);
}

#if defined(CONFIG_DISPLAY_ARTINCHIP_RGB_SHELL)
static const struct device *const artinchip_rgb_shell_dev = DEVICE_DT_GET(DT_DRV_INST(0));

static int artinchip_rgb_shell_status(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_rgb_shell_dev;
	const struct artinchip_rgb_config *config = dev->config;
	const struct artinchip_rgb_data *data = dev->data;
	uint32_t pixclk_rate;
	uint32_t sclk_rate;
	int pix_ret;
	int sclk_ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	pix_ret = clock_control_get_rate(config->clock_dev, config->clock_id, &pixclk_rate);
	sclk_ret = clock_control_get_rate(config->clock_dev,
					  (clock_control_subsys_t)UINT_TO_POINTER(
						  ARTINCHIP_CLK_MIPIDSI),
					  &sclk_rate);
	shell_print(sh, "initialized: %s enabled: %s manual-start: %s",
		    data->initialized ? "yes" : "no", data->enabled ? "yes" : "no",
		    config->manual_start ? "yes" : "no");
	shell_print(sh, "mode=%u format=%u data-order=0x%08x clock-phase=%u mirror=%s",
		    config->mode, config->format, config->data_order, config->clock_phase,
		    config->data_mirror ? "yes" : "no");
	shell_print(sh, "sclk-rate: %s%u Hz", sclk_ret == 0 ? "" : "unavailable ",
		    sclk_ret == 0 ? sclk_rate : 0U);
	shell_print(sh, "pixclk-rate (PCK): %s%u Hz", pix_ret == 0 ? "" : "unavailable ",
		    pix_ret == 0 ? pixclk_rate : 0U);
	shell_print(sh, "lcd_ctl=0x%08x clk_ctl=0x%08x data_sel=0x%08x data_seq=0x%08x version=0x%08x",
		    artinchip_rgb_read(config, RGB_LCD_CTL),
		    artinchip_rgb_read(config, RGB_CLK_CTL),
		    artinchip_rgb_read(config, RGB_DATA_SEL),
		    artinchip_rgb_read(config, RGB_DATA_SEQ_SEL),
		    artinchip_rgb_read(config, RGB_VERSION));

	return 0;
}

static int artinchip_rgb_shell_enable(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = artinchip_rgb_enable(artinchip_rgb_shell_dev);
	if (ret != 0) {
		shell_error(sh, "RGB enable failed: %d", ret);
		return ret;
	}

	shell_print(sh, "RGB output enabled");
	return 0;
}

static int artinchip_rgb_shell_disable(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	(void)artinchip_rgb_disable(artinchip_rgb_shell_dev);
	shell_print(sh, "RGB output disabled");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_aicrgb,
	SHELL_CMD(disable, NULL, "Disable RGB output", artinchip_rgb_shell_disable),
	SHELL_CMD(enable, NULL, "Enable RGB output clock/reset/registers", artinchip_rgb_shell_enable),
	SHELL_CMD(status, NULL, "Show RGB output register state", artinchip_rgb_shell_status),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(aicrgb, &sub_aicrgb, "ArtInChip RGB output diagnostics", NULL);
#endif

#define ARTINCHIP_RGB_INIT(inst)								\
	PINCTRL_DT_INST_DEFINE(inst);								\
	static struct artinchip_rgb_data artinchip_rgb_data_##inst;			\
	static const struct artinchip_rgb_config artinchip_rgb_config_##inst = {		\
		.base = DT_INST_REG_ADDR(inst),							\
		.mode = DT_INST_PROP(inst, artinchip_mode),				\
		.format = DT_INST_PROP(inst, artinchip_format),				\
		.data_order = DT_INST_PROP(inst, artinchip_data_order),			\
		.clock_phase = DT_INST_PROP(inst, artinchip_clock_phase),		\
		.data_mirror = DT_INST_PROP(inst, artinchip_data_mirror),		\
		.manual_start = DT_INST_PROP(inst, artinchip_manual_start),		\
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),				\
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),			\
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),	\
		.reset = RESET_DT_SPEC_INST_GET(inst),					\
	};											\
	DEVICE_DT_INST_DEFINE(inst, artinchip_rgb_init, NULL,				\
			      &artinchip_rgb_data_##inst, &artinchip_rgb_config_##inst,	\
			      POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(ARTINCHIP_RGB_INIT)
