/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_dvp

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/video.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_VIDEO_ARTINCHIP_DVP_SHELL)
#include <stdlib.h>
#include <zephyr/shell/shell.h>
#endif

#include "video_device.h"

LOG_MODULE_REGISTER(video_artinchip_dvp, CONFIG_VIDEO_LOG_LEVEL);

#define DVP_CTL            0x000U
#define DVP_CH_IRQ_STA     0x00CU
#define DVP_IRQ_EN         0x100U
#define DVP_IRQ_STA        0x104U
#define DVP_IRQ_CFG        0x108U
#define DVP_IN_CFG         0x10CU
#define DVP_OUT_HOR_SIZE   0x120U
#define DVP_OUT_VER_SIZE   0x128U
#define DVP_OUT_FRA_NUM    0x130U
#define DVP_OUT_CTL        0x138U
#define DVP_OUT_UPDATE_CTL 0x13CU
#define DVP_OUT_ADDR_BUF0  0x140U
#define DVP_OUT_ADDR_BUF1  0x144U
#define DVP_OUT_LINE_STRIDE0 0x150U
#define DVP_OUT_LINE_STRIDE1 0x154U
#define DVP_VER            0xFFCU

#define DVP_CTL_OUT_FMT_MASK GENMASK(14, 12)
#define DVP_CTL_OUT_FMT(x)   FIELD_PREP(DVP_CTL_OUT_FMT_MASK, (x))
#define DVP_CTL_DROP_FRAME_EN BIT(2)
#define DVP_CTL_IN_SEQ_MASK  GENMASK(9, 8)
#define DVP_CTL_IN_SEQ(x)    FIELD_PREP(DVP_CTL_IN_SEQ_MASK, (x))
#define DVP_CTL_IN_FMT_MASK  GENMASK(6, 4)
#define DVP_CTL_IN_FMT(x)    FIELD_PREP(DVP_CTL_IN_FMT_MASK, (x))
#define DVP_CTL_VIDEO_CONTINUE_EN BIT(3)
#define DVP_CTL_CLR         BIT(1)
#define DVP_CTL_EN          BIT(0)

#define DVP_IRQ_UPDATE_DONE BIT(7)
#define DVP_IRQ_XY_CODE_ERR BIT(6)
#define DVP_IRQ_BUF_FULL    BIT(3)
#define DVP_IRQ_HNUM        BIT(2)
#define DVP_IRQ_FRAME_DONE  BIT(1)
#define DVP_IRQ_ERRORS      (DVP_IRQ_XY_CODE_ERR | DVP_IRQ_BUF_FULL)
#define DVP_IRQ_MASK        (DVP_IRQ_UPDATE_DONE | DVP_IRQ_ERRORS | DVP_IRQ_HNUM | \
			     DVP_IRQ_FRAME_DONE)

#define DVP_IRQ_CFG_HNUM_SHIFT 16U
#define DVP_CH0_IRQ_STA BIT(0)

#define DVP_IN_CFG_VSYNC_POL_FALLING BIT(2)
#define DVP_IN_CFG_HREF_POL_ACTIVE_HIGH BIT(1)
#define DVP_IN_CFG_PCLK_POL_FALLING BIT(0)

#define DVP_IN_YUV422 1U
#define DVP_OUT_YUV422_COMBINED_NV16 1U
#define DVP_OUT_YUV420_COMBINED_NV12 2U
#define DVP_SEQ_YUYV 0U

#define DVP_OUT_HOR_NUM(width) ((((width) * 2U) - 1U) << 16)
#define DVP_OUT_VER_NUM(height) (((height) - 1U) << 16)
#define DVP_OUT_CTL_CAP_ON BIT(0)

#define DVP_WIDTH_MIN  160U
#define DVP_WIDTH_MAX  1920U
#define DVP_HEIGHT_MIN 120U
#define DVP_HEIGHT_MAX 1080U
#define DVP_BUF_ALIGN  64U
#define DVP_STRIDE_ALIGN 8U

enum artinchip_dvp_variant {
	ARTINCHIP_DVP_VARIANT_V1,
	ARTINCHIP_DVP_VARIANT_V2,
};

struct artinchip_dvp_config {
	uintptr_t base;
	enum artinchip_dvp_variant variant;
	uint8_t data_width;
	uint32_t input_cfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_id;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pincfg;
	void (*irq_config_func)(const struct device *dev);
};

struct artinchip_dvp_data {
	struct video_format fmt;
	struct k_fifo fifo_in;
	struct k_fifo fifo_out;
	struct k_spinlock lock;
	struct video_buffer *active;
	struct video_buffer *next;
#ifdef CONFIG_POLL
	struct k_poll_signal *signal;
#endif
	bool streaming;
	uint32_t irq_count;
	uint32_t frame_count;
	uint32_t error_count;
};

static const struct video_format_cap artinchip_dvp_format_caps[] = {
	{
		.pixelformat = VIDEO_PIX_FMT_NV16,
		.width_min = DVP_WIDTH_MIN,
		.width_max = DVP_WIDTH_MAX,
		.height_min = DVP_HEIGHT_MIN,
		.height_max = DVP_HEIGHT_MAX,
		.width_step = 2,
		.height_step = 2,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_NV12,
		.width_min = DVP_WIDTH_MIN,
		.width_max = DVP_WIDTH_MAX,
		.height_min = DVP_HEIGHT_MIN,
		.height_max = DVP_HEIGHT_MAX,
		.width_step = 2,
		.height_step = 2,
	},
	{ 0 },
};

static uint32_t artinchip_dvp_read(const struct artinchip_dvp_config *config, uint32_t offset)
{
	return sys_read32(config->base + offset);
}

static void artinchip_dvp_write(const struct artinchip_dvp_config *config, uint32_t offset,
				uint32_t value)
{
	sys_write32(value, config->base + offset);
}

static bool artinchip_dvp_irq_pending(const struct artinchip_dvp_config *config)
{
	uint32_t pending;

	if (config->variant == ARTINCHIP_DVP_VARIANT_V1) {
		return true;
	}

	pending = artinchip_dvp_read(config, DVP_CH_IRQ_STA);
	if (pending != 0U) {
		artinchip_dvp_write(config, DVP_CH_IRQ_STA, pending);
	}

	return (pending & DVP_CH0_IRQ_STA) != 0U;
}

static void artinchip_dvp_capture_stop(const struct artinchip_dvp_config *config)
{
	artinchip_dvp_write(config, DVP_IRQ_EN, 0U);
	artinchip_dvp_write(config, DVP_OUT_CTL, 0U);
	artinchip_dvp_write(config, DVP_CTL, 0U);
}

static void artinchip_dvp_program_buffer(const struct device *dev, struct video_buffer *vbuf)
{
	const struct artinchip_dvp_config *config = dev->config;
	struct artinchip_dvp_data *data = dev->data;
	uintptr_t y_addr = (uintptr_t)vbuf->buffer;
	uintptr_t uv_addr = y_addr + (data->fmt.pitch * data->fmt.height);

	artinchip_dvp_write(config, DVP_OUT_ADDR_BUF0, (uint32_t)y_addr);
	artinchip_dvp_write(config, DVP_OUT_ADDR_BUF1, (uint32_t)uv_addr);
	artinchip_dvp_write(config, DVP_OUT_LINE_STRIDE0, data->fmt.pitch);
	artinchip_dvp_write(config, DVP_OUT_LINE_STRIDE1, data->fmt.pitch);
	artinchip_dvp_write(config, DVP_OUT_UPDATE_CTL, 1U);
}

static uint32_t artinchip_dvp_output_format(uint32_t pixelformat)
{
	return pixelformat == VIDEO_PIX_FMT_NV12 ? DVP_OUT_YUV420_COMBINED_NV12 :
						   DVP_OUT_YUV422_COMBINED_NV16;
}

static void artinchip_dvp_capture_start(const struct device *dev, struct video_buffer *vbuf)
{
	const struct artinchip_dvp_config *config = dev->config;
	struct artinchip_dvp_data *data = dev->data;
	uint32_t ctl;

	artinchip_dvp_write(config, DVP_CTL, DVP_CTL_CLR);
	artinchip_dvp_write(config, DVP_IN_CFG, config->input_cfg);
	artinchip_dvp_write(config, DVP_OUT_HOR_SIZE, DVP_OUT_HOR_NUM(data->fmt.width));
	artinchip_dvp_write(config, DVP_OUT_VER_SIZE, DVP_OUT_VER_NUM(data->fmt.height));
	artinchip_dvp_write(config, DVP_OUT_FRA_NUM, 0x80000003U);
	artinchip_dvp_write(config, DVP_IRQ_CFG,
			    (data->fmt.height / 4U) << DVP_IRQ_CFG_HNUM_SHIFT);
	artinchip_dvp_program_buffer(dev, vbuf);

	ctl = DVP_CTL_IN_FMT(DVP_IN_YUV422) | DVP_CTL_IN_SEQ(DVP_SEQ_YUYV) |
	      DVP_CTL_OUT_FMT(artinchip_dvp_output_format(data->fmt.pixelformat)) |
	      DVP_CTL_VIDEO_CONTINUE_EN | DVP_CTL_DROP_FRAME_EN | DVP_CTL_EN;

	artinchip_dvp_write(config, DVP_IRQ_STA, DVP_IRQ_MASK);
	artinchip_dvp_write(config, DVP_IRQ_EN, DVP_IRQ_MASK);
	artinchip_dvp_write(config, DVP_CTL, ctl);
	artinchip_dvp_write(config, DVP_OUT_CTL, DVP_OUT_CTL_CAP_ON);
}

static int artinchip_dvp_fill_format(struct video_format *fmt)
{
	if (fmt->type != VIDEO_BUF_TYPE_OUTPUT) {
		return -EINVAL;
	}

	if ((fmt->pixelformat != VIDEO_PIX_FMT_NV16) && (fmt->pixelformat != VIDEO_PIX_FMT_NV12)) {
		return -ENOTSUP;
	}

	if ((fmt->width < DVP_WIDTH_MIN) || (fmt->width > DVP_WIDTH_MAX) ||
	    (fmt->height < DVP_HEIGHT_MIN) || (fmt->height > DVP_HEIGHT_MAX) ||
	    ((fmt->width % 2U) != 0U) || ((fmt->height % 2U) != 0U)) {
		return -ENOTSUP;
	}

	fmt->pitch = ROUND_UP(fmt->width, DVP_STRIDE_ALIGN);
	fmt->size = fmt->pitch * fmt->height;

	if (fmt->pixelformat == VIDEO_PIX_FMT_NV12) {
		fmt->size += fmt->size / 2U;
	} else {
		fmt->size *= 2U;
	}

	return 0;
}

static int artinchip_dvp_set_format(const struct device *dev, struct video_format *fmt)
{
	struct artinchip_dvp_data *data = dev->data;
	k_spinlock_key_t key;
	int ret;

	ret = artinchip_dvp_fill_format(fmt);
	if (ret != 0) {
		return ret;
	}

	key = k_spin_lock(&data->lock);
	if (data->streaming) {
		k_spin_unlock(&data->lock, key);
		return -EBUSY;
	}
	data->fmt = *fmt;
	k_spin_unlock(&data->lock, key);

	return 0;
}

static int artinchip_dvp_get_format(const struct device *dev, struct video_format *fmt)
{
	struct artinchip_dvp_data *data = dev->data;

	*fmt = data->fmt;
	return 0;
}

static int artinchip_dvp_get_caps(const struct device *dev, struct video_caps *caps)
{
	ARG_UNUSED(dev);

	caps->type = VIDEO_BUF_TYPE_OUTPUT;
	caps->format_caps = artinchip_dvp_format_caps;
	caps->min_vbuf_count = 1U;
	caps->buf_align = DVP_BUF_ALIGN;

	return 0;
}

static int artinchip_dvp_enqueue(const struct device *dev, struct video_buffer *vbuf)
{
	struct artinchip_dvp_data *data = dev->data;

	if ((vbuf == NULL) || (vbuf->type != VIDEO_BUF_TYPE_OUTPUT) ||
	    (vbuf->buffer == NULL)) {
		return -EINVAL;
	}

	if ((((uintptr_t)vbuf->buffer) % DVP_BUF_ALIGN) != 0U) {
		return -EINVAL;
	}

	if (vbuf->size < data->fmt.size) {
		return -ENOMEM;
	}

	vbuf->bytesused = 0U;
	vbuf->line_offset = 0U;
	k_fifo_put(&data->fifo_in, vbuf);

	return 0;
}

static int artinchip_dvp_dequeue(const struct device *dev, struct video_buffer **vbufp,
				 k_timeout_t timeout)
{
	struct artinchip_dvp_data *data = dev->data;

	*vbufp = k_fifo_get(&data->fifo_out, timeout);
	if (*vbufp == NULL) {
		return -EAGAIN;
	}

	return 0;
}

static int artinchip_dvp_flush(const struct device *dev, bool cancel)
{
	const struct artinchip_dvp_config *config = dev->config;
	struct artinchip_dvp_data *data = dev->data;
	struct video_buffer *vbuf;
	k_spinlock_key_t key;

	ARG_UNUSED(cancel);

	key = k_spin_lock(&data->lock);
	artinchip_dvp_capture_stop(config);
	data->streaming = false;
	vbuf = data->active;
	data->active = NULL;
	if (data->next != NULL) {
		k_fifo_put(&data->fifo_out, data->next);
		data->next = NULL;
	}
	k_spin_unlock(&data->lock, key);

	if (vbuf != NULL) {
		k_fifo_put(&data->fifo_out, vbuf);
	}

	while ((vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT)) != NULL) {
		k_fifo_put(&data->fifo_out, vbuf);
	}

	return 0;
}

static int artinchip_dvp_set_stream(const struct device *dev, bool enable,
				    enum video_buf_type type)
{
	const struct artinchip_dvp_config *config = dev->config;
	struct artinchip_dvp_data *data = dev->data;
	struct video_buffer *vbuf;
	k_spinlock_key_t key;

	if (type != VIDEO_BUF_TYPE_OUTPUT) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	if (!enable) {
		artinchip_dvp_capture_stop(config);
		data->streaming = false;
		vbuf = data->active;
		data->active = NULL;
		if (data->next != NULL) {
			k_fifo_put(&data->fifo_out, data->next);
			data->next = NULL;
		}
		k_spin_unlock(&data->lock, key);

		if (vbuf != NULL) {
			k_fifo_put(&data->fifo_out, vbuf);
		}
		return 0;
	}

	if (data->streaming) {
		k_spin_unlock(&data->lock, key);
		return -EBUSY;
	}

	vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT);
	if (vbuf == NULL) {
		k_spin_unlock(&data->lock, key);
		return -EAGAIN;
	}

	data->active = vbuf;
	data->next = NULL;
	data->streaming = true;
	artinchip_dvp_capture_start(dev, vbuf);
	k_spin_unlock(&data->lock, key);

	return 0;
}

#ifdef CONFIG_POLL
static int artinchip_dvp_set_signal(const struct device *dev, struct k_poll_signal *sig)
{
	struct artinchip_dvp_data *data = dev->data;

	if ((data->signal != NULL) && (sig != NULL)) {
		return -EALREADY;
	}

	data->signal = sig;
	return 0;
}
#endif

static void artinchip_dvp_isr(const struct device *dev)
{
	const struct artinchip_dvp_config *config = dev->config;
	struct artinchip_dvp_data *data = dev->data;
	struct video_buffer *done = NULL;
	struct video_buffer *next;
	uint32_t status;

	if (!artinchip_dvp_irq_pending(config)) {
		return;
	}

	status = artinchip_dvp_read(config, DVP_IRQ_STA) & DVP_IRQ_MASK;
	if (status == 0U) {
		return;
	}

	artinchip_dvp_write(config, DVP_IRQ_STA, status);
	data->irq_count++;

	if ((status & DVP_IRQ_ERRORS) != 0U) {
		data->error_count++;
	}

	if ((status & DVP_IRQ_HNUM) != 0U && data->next == NULL) {
		next = k_fifo_get(&data->fifo_in, K_NO_WAIT);
		if (next != NULL) {
			artinchip_dvp_program_buffer(dev, next);
			data->next = next;
		}
	}

	if ((status & DVP_IRQ_FRAME_DONE) == 0U) {
		return;
	}

	done = data->active;
	if (done != NULL) {
		done->bytesused = data->fmt.size;
		done->timestamp = k_uptime_get_32();
		done->line_offset = 0U;
		k_fifo_put(&data->fifo_out, done);
		data->frame_count++;
#ifdef CONFIG_POLL
		if (data->signal != NULL) {
			k_poll_signal_raise(data->signal, VIDEO_BUF_DONE);
		}
#endif
	}

	if (data->next != NULL) {
		data->active = data->next;
		data->next = NULL;
		return;
	}

	data->active = NULL;
	data->streaming = false;
	artinchip_dvp_capture_stop(config);
}

static DEVICE_API(video, artinchip_dvp_api) = {
	.set_format = artinchip_dvp_set_format,
	.get_format = artinchip_dvp_get_format,
	.set_stream = artinchip_dvp_set_stream,
	.get_caps = artinchip_dvp_get_caps,
	.enqueue = artinchip_dvp_enqueue,
	.dequeue = artinchip_dvp_dequeue,
	.flush = artinchip_dvp_flush,
#ifdef CONFIG_POLL
	.set_signal = artinchip_dvp_set_signal,
#endif
};

static int artinchip_dvp_init(const struct device *dev)
{
	const struct artinchip_dvp_config *config = dev->config;
	struct artinchip_dvp_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->clock_dev)) {
		return -ENODEV;
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
		return ret;
	}

	data->fmt.type = VIDEO_BUF_TYPE_OUTPUT;
	data->fmt.pixelformat = VIDEO_PIX_FMT_NV16;
	data->fmt.width = 640U;
	data->fmt.height = 480U;
	ret = artinchip_dvp_fill_format(&data->fmt);
	if (ret != 0) {
		return ret;
	}

	k_fifo_init(&data->fifo_in);
	k_fifo_init(&data->fifo_out);
	artinchip_dvp_capture_stop(config);
	artinchip_dvp_write(config, DVP_IRQ_STA, DVP_IRQ_MASK);

	config->irq_config_func(dev);

	return 0;
}

#if defined(CONFIG_VIDEO_ARTINCHIP_DVP_SHELL)
static const struct device *artinchip_dvp_shell_dev(void)
{
#if DT_HAS_CHOSEN(zephyr_camera)
	return DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));
#elif DT_HAS_COMPAT_STATUS_OKAY(artinchip_d13x_dvp)
	return DEVICE_DT_GET(DT_INST(0, artinchip_d13x_dvp));
#else
	return DEVICE_DT_GET(DT_INST(0, artinchip_d12p_dvp));
#endif
}

static int artinchip_dvp_shell_status(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_dvp_shell_dev();
	const struct artinchip_dvp_config *config = dev->config;
	struct artinchip_dvp_data *data = dev->data;
	enum clock_control_status clk_status;
	uint32_t rate = 0U;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(dev)) {
		shell_error(sh, "DVP device is not ready");
		return -ENODEV;
	}

	clk_status = clock_control_get_status(config->clock_dev, config->clock_id);
	(void)clock_control_get_rate(config->clock_dev, config->clock_id, &rate);

	shell_print(sh, "device: %s", dev->name);
	shell_print(sh, "variant: v%u", config->variant == ARTINCHIP_DVP_VARIANT_V2 ? 2U : 1U);
	shell_print(sh, "clock: %s, %u Hz",
		    clk_status == CLOCK_CONTROL_STATUS_ON ? "on" : "off", rate);
	shell_print(sh, "bus: %u-bit", config->data_width);
	shell_print(sh, "format: fourcc 0x%08x %ux%u pitch %u size %u",
		    data->fmt.pixelformat, data->fmt.width, data->fmt.height, data->fmt.pitch,
		    data->fmt.size);
	shell_print(sh, "streaming: %s", data->streaming ? "yes" : "no");
	shell_print(sh, "irq/frame/error: %u/%u/%u", data->irq_count, data->frame_count,
		    data->error_count);
	shell_print(sh, "ver=0x%08x ctl=0x%08x in_cfg=0x%08x irq_en=0x%08x irq_sta=0x%08x",
		    artinchip_dvp_read(config, DVP_VER), artinchip_dvp_read(config, DVP_CTL),
		    artinchip_dvp_read(config, DVP_IN_CFG),
		    artinchip_dvp_read(config, DVP_IRQ_EN),
		    artinchip_dvp_read(config, DVP_IRQ_STA));

	return 0;
}

static int artinchip_dvp_shell_power(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_dvp_shell_dev();
	const struct artinchip_dvp_config *config = dev->config;
	bool on;
	int ret;

	if (argc != 2U) {
		return -EINVAL;
	}

	on = strcmp(argv[1], "on") == 0;
	if (!on && (strcmp(argv[1], "off") != 0)) {
		return -EINVAL;
	}

	ret = on ? clock_control_on(config->clock_dev, config->clock_id)
		 : clock_control_off(config->clock_dev, config->clock_id);
	if (ret != 0) {
		shell_error(sh, "clock %s failed: %d", on ? "on" : "off", ret);
		return ret;
	}

	shell_print(sh, "DVP clock %s", on ? "enabled" : "disabled");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	artinchip_dvp_cmds,
	SHELL_CMD(status, NULL, "Show DVP state and key registers.", artinchip_dvp_shell_status),
	SHELL_CMD_ARG(power, NULL, "Enable or disable the DVP clock: power <on|off>.",
		      artinchip_dvp_shell_power, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(aicdvp, &artinchip_dvp_cmds, "ArtInChip DVP diagnostics.", NULL);
#endif

#define ARTINCHIP_DVP_INPUT_CFG(inst)                                                             \
	((DT_INST_ENUM_IDX(inst, pclk_sample) == 1 ? DVP_IN_CFG_PCLK_POL_FALLING : 0U) |          \
	 (DT_INST_ENUM_IDX(inst, hsync_active) == 1 ? DVP_IN_CFG_HREF_POL_ACTIVE_HIGH : 0U) |     \
	 (DT_INST_ENUM_IDX(inst, vsync_active) == 0 ? DVP_IN_CFG_VSYNC_POL_FALLING : 0U))

#define ARTINCHIP_DVP_INIT(inst, variant_name)                                                     \
	PINCTRL_DT_INST_DEFINE(inst);                                                             \
	static void artinchip_dvp_irq_config_##variant_name##_##inst(const struct device *dev)    \
	{                                                                                         \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority), artinchip_dvp_isr,    \
			    DEVICE_DT_INST_GET(inst), 0);                                           \
		irq_enable(DT_INST_IRQN(inst));                                                    \
	}                                                                                         \
	static const struct artinchip_dvp_config artinchip_dvp_config_##variant_name##_##inst = { \
		.base = DT_INST_REG_ADDR(inst),                                                     \
		.variant = ARTINCHIP_DVP_VARIANT_##variant_name,                                   \
		.data_width = DT_INST_PROP(inst, data_width),                                       \
		.input_cfg = ARTINCHIP_DVP_INPUT_CFG(inst),                                        \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),               \
		.reset = RESET_DT_SPEC_INST_GET(inst),                                             \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                    \
		.irq_config_func = artinchip_dvp_irq_config_##variant_name##_##inst,                \
	};                                                                                        \
	static struct artinchip_dvp_data artinchip_dvp_data_##variant_name##_##inst;              \
	DEVICE_DT_INST_DEFINE(inst, artinchip_dvp_init, NULL,                                     \
			      &artinchip_dvp_data_##variant_name##_##inst,                         \
			      &artinchip_dvp_config_##variant_name##_##inst, POST_KERNEL,          \
			      CONFIG_VIDEO_INIT_PRIORITY, &artinchip_dvp_api);                    \
	VIDEO_DEVICE_DEFINE(artinchip_dvp_vdev_##variant_name##_##inst, DEVICE_DT_INST_GET(inst), \
			    NULL);

#define ARTINCHIP_DVP_INIT_V1(inst) ARTINCHIP_DVP_INIT(inst, V1)
#define ARTINCHIP_DVP_INIT_V2(inst) ARTINCHIP_DVP_INIT(inst, V2)

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT artinchip_d12p_dvp
DT_INST_FOREACH_STATUS_OKAY(ARTINCHIP_DVP_INIT_V1)

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT artinchip_d13x_dvp
DT_INST_FOREACH_STATUS_OKAY(ARTINCHIP_DVP_INIT_V2)
