/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_codec

#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define CODEC_TX_DVC             0x20U
#define CODEC_TX_PLAYBACK_CTRL   0x24U
#define CODEC_TXFIFO_CTRL        0x34U
#define CODEC_GLOBE_CTL          0x60U

#define CODEC_TX_DVC3_GAIN_MASK  GENMASK(23, 16)
#define CODEC_TX_DVC4_GAIN_MASK  GENMASK(31, 24)
#define CODEC_TX_DVC_ENABLE      (BIT(0) | BIT(1))
#define CODEC_TX_FS_MASK         GENMASK(3, 1)
#define CODEC_TX_CLK_44K_FAMILY  BIT(0)
#define CODEC_TX_IF_ENABLE       (BIT(4) | BIT(5) | BIT(6))
#define CODEC_TXFIFO_FLUSH       BIT(31)
#define CODEC_TXFIFO_CHANNELS    GENMASK(1, 0)
#define CODEC_TX_GLOBAL_ENABLE   BIT(1)

struct codec_artinchip_config {
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_id;
	struct reset_dt_spec reset;
};

struct codec_artinchip_data {
	bool configured;
	bool muted;
};

static void codec_artinchip_update(uintptr_t addr, uint32_t mask, uint32_t value)
{
	sys_write32((sys_read32(addr) & ~mask) | value, addr);
}

static int codec_artinchip_rate_code(uint32_t rate)
{
	switch (rate) {
	case AUDIO_PCM_RATE_48K:
	case AUDIO_PCM_RATE_44P1K:
		return 0;
	case AUDIO_PCM_RATE_32K:
		return 1;
	case AUDIO_PCM_RATE_24K:
	case AUDIO_PCM_RATE_22P05K:
		return 2;
	case AUDIO_PCM_RATE_16K:
		return 3;
	case AUDIO_PCM_RATE_11P025K:
		return 4;
	case AUDIO_PCM_RATE_8K:
		return 5;
	default:
		return -EINVAL;
	}
}

static int codec_artinchip_configure(const struct device *dev, struct audio_codec_cfg *cfg)
{
	const struct codec_artinchip_config *config = dev->config;
	struct codec_artinchip_data *data = dev->data;
	const struct pcm_config *pcm;
	uint32_t playback;
	int rate_code;

	if ((cfg == NULL) || (cfg->dai_type != AUDIO_DAI_TYPE_PCM) ||
	    (cfg->dai_route != AUDIO_ROUTE_PLAYBACK)) {
		return -ENOTSUP;
	}

	pcm = &cfg->dai_cfg.pcm;
	if ((pcm->pcm_width != AUDIO_PCM_WIDTH_16_BITS) || (pcm->channels == 0U) ||
	    (pcm->channels > 2U) || ((pcm->dir & AUDIO_DAI_DIR_TX) == 0U)) {
		return -EINVAL;
	}

	rate_code = codec_artinchip_rate_code(pcm->samplerate);
	if (rate_code < 0) {
		return rate_code;
	}

	playback = FIELD_PREP(CODEC_TX_FS_MASK, rate_code) | CODEC_TX_IF_ENABLE;
	if ((pcm->samplerate == AUDIO_PCM_RATE_44P1K) ||
	    (pcm->samplerate == AUDIO_PCM_RATE_22P05K) ||
	    (pcm->samplerate == AUDIO_PCM_RATE_11P025K)) {
		playback |= CODEC_TX_CLK_44K_FAMILY;
	}

	sys_write32(playback, config->base + CODEC_TX_PLAYBACK_CTRL);
	sys_write32(CODEC_TXFIFO_FLUSH | FIELD_PREP(CODEC_TXFIFO_CHANNELS,
						    BIT(pcm->channels) - 1U),
		    config->base + CODEC_TXFIFO_CTRL);
	codec_artinchip_update(config->base + CODEC_TX_DVC, CODEC_TX_DVC_ENABLE, CODEC_TX_DVC_ENABLE);
	data->configured = true;
	return 0;
}

static void codec_artinchip_start_output(const struct device *dev)
{
	const struct codec_artinchip_config *config = dev->config;
	struct codec_artinchip_data *data = dev->data;

	if (data->configured && !data->muted) {
		codec_artinchip_update(config->base + CODEC_GLOBE_CTL, CODEC_TX_GLOBAL_ENABLE,
				  CODEC_TX_GLOBAL_ENABLE);
	}
}

static void codec_artinchip_stop_output(const struct device *dev)
{
	const struct codec_artinchip_config *config = dev->config;

	codec_artinchip_update(config->base + CODEC_GLOBE_CTL, CODEC_TX_GLOBAL_ENABLE, 0U);
}

static int codec_artinchip_set_property(const struct device *dev, audio_property_t property,
				    audio_channel_t channel, audio_property_value_t val)
{
	const struct codec_artinchip_config *config = dev->config;
	struct codec_artinchip_data *data = dev->data;
	uint32_t volume;

	if ((channel != AUDIO_CHANNEL_ALL) && (channel != AUDIO_CHANNEL_FRONT_LEFT) &&
	    (channel != AUDIO_CHANNEL_FRONT_RIGHT)) {
		return -ENOTSUP;
	}

	switch (property) {
	case AUDIO_PROPERTY_OUTPUT_VOLUME:
		if ((val.vol < 0) || (val.vol > UINT8_MAX)) {
			return -EINVAL;
		}
		volume = FIELD_PREP(CODEC_TX_DVC3_GAIN_MASK, val.vol) |
			 FIELD_PREP(CODEC_TX_DVC4_GAIN_MASK, val.vol);
		codec_artinchip_update(config->base + CODEC_TX_DVC,
				  CODEC_TX_DVC3_GAIN_MASK | CODEC_TX_DVC4_GAIN_MASK, volume);
		return 0;
	case AUDIO_PROPERTY_OUTPUT_MUTE:
		data->muted = val.mute;
		codec_artinchip_update(config->base + CODEC_GLOBE_CTL, CODEC_TX_GLOBAL_ENABLE,
				  val.mute ? 0U : CODEC_TX_GLOBAL_ENABLE);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int codec_artinchip_apply_properties(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int codec_artinchip_init(const struct device *dev)
{
	const struct codec_artinchip_config *config = dev->config;
	int ret;

	if (!device_is_ready(config->clock_dev) || !device_is_ready(config->reset.dev)) {
		return -ENODEV;
	}
	ret = clock_control_on(config->clock_dev, config->clock_id);
	if (ret != 0) {
		return ret;
	}
	ret = reset_line_toggle_dt(&config->reset);
	if (ret != 0) {
		return ret;
	}
	sys_write32(CODEC_TXFIFO_FLUSH, config->base + CODEC_TXFIFO_CTRL);
	return 0;
}

static DEVICE_API(audio_codec, codec_artinchip_api) = {
	.configure = codec_artinchip_configure,
	.start_output = codec_artinchip_start_output,
	.stop_output = codec_artinchip_stop_output,
	.set_property = codec_artinchip_set_property,
	.apply_properties = codec_artinchip_apply_properties,
};

#define CODEC_ARTINCHIP_INIT(inst)                                                                      \
	static struct codec_artinchip_data codec_artinchip_data_##inst;                                      \
	static const struct codec_artinchip_config codec_artinchip_config_##inst = {                          \
		.base = DT_INST_REG_ADDR(inst),                                                       \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                                \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),                 \
		.reset = RESET_DT_SPEC_INST_GET(inst),                                                \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, codec_artinchip_init, NULL, &codec_artinchip_data_##inst,                 \
			      &codec_artinchip_config_##inst, POST_KERNEL, CONFIG_AUDIO_CODEC_INIT_PRIORITY, \
			      &codec_artinchip_api);

DT_INST_FOREACH_STATUS_OKAY(CODEC_ARTINCHIP_INIT)
