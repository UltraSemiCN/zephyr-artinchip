/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_dma

#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(dma_artinchip, CONFIG_DMA_LOG_LEVEL);

#define AIC_DMA_CHAN_OFFSET      0x40U
#define AIC_DMA_CHAN_BASE        0x100U
#define AIC_DMA_IRQ_EN_REG       0x00U
#define AIC_DMA_IRQ_STA_REG      0x10U
#define AIC_DMA_CH_EN_REG        0x00U
#define AIC_DMA_CH_PAUSE_REG     0x04U
#define AIC_DMA_CH_TASK_REG      0x08U
#define AIC_DMA_CH_MODE_REG      0x28U
#define AIC_DMA_CH_LEFT_REG      0x18U
#define AIC_DMA_LINK_END_FLAG    0xFFFFF800U
#define AIC_DMA_IRQ_ALL_TASK     BIT(2)
#define AIC_DMA_IRQ_SHIFT(ch)    (4U * (ch))
#define AIC_DMA_IRQ_MASK(ch)     (GENMASK(2, 0) << AIC_DMA_IRQ_SHIFT(ch))
#define AIC_DMA_ID_DRAM          0U
#define AIC_DMA_TASK_DELAY       0x40U
#define AIC_DMA_MODE_MEM_TO_DEV  0x28U
#define AIC_DMA_MODE_DEV_TO_MEM  0x14U

struct artinchip_dma_task {
	uint32_t cfg;
	uint32_t src;
	uint32_t dst;
	uint32_t len;
	uint32_t delay;
	uint32_t p_next;
	uint32_t mode;
	uint32_t pad[7];
	struct artinchip_dma_task *v_next;
};

struct artinchip_dma_channel {
	struct artinchip_dma_task task __aligned(8);
	dma_callback_t cb;
	void *user_data;
	bool active;
};

struct artinchip_dma_config {
	uintptr_t base;
	uint8_t channel_count;
	void (*irq_config)(const struct device *dev);
};

struct artinchip_dma_data {
	struct dma_context dma_ctx;
	struct artinchip_dma_channel channels[DT_INST_PROP(0, dma_channels)];
	ATOMIC_DEFINE(channels_atomic, DT_INST_PROP(0, dma_channels));
};

static uint32_t artinchip_dma_channel_base(const struct artinchip_dma_config *config, uint32_t channel)
{
	return config->base + AIC_DMA_CHAN_BASE + (channel * AIC_DMA_CHAN_OFFSET);
}

static uint32_t artinchip_dma_build_task_cfg(uint32_t slave_id, bool mem_to_dev)
{
	uint32_t cfg = (1U << 9) | (1U << 25) | (1U << 6) | (1U << 22);

	if (mem_to_dev) {
		cfg |= (AIC_DMA_ID_DRAM << 0) | ((slave_id & 0x3FU) << 16) |
		      (0U << 8) | (1U << 24);
	} else {
		cfg |= (AIC_DMA_ID_DRAM << 16) | ((slave_id & 0x3FU) << 0) |
		      (1U << 8) | (0U << 24);
	}

	return cfg;
}

static int artinchip_dma_start_transfer(const struct device *dev, uint32_t channel,
				   struct artinchip_dma_task *task)
{
	const struct artinchip_dma_config *config = dev->config;
	uintptr_t ch_base = artinchip_dma_channel_base(config, channel);
	uint32_t irq_en;

	sys_write32(task->mode, ch_base + AIC_DMA_CH_MODE_REG);
	sys_write32((uint32_t)(uintptr_t)task, ch_base + AIC_DMA_CH_TASK_REG);
	sys_write32(0U, ch_base + AIC_DMA_CH_PAUSE_REG);
	sys_write32(1U, ch_base + AIC_DMA_CH_EN_REG);

	irq_en = sys_read32(config->base + AIC_DMA_IRQ_EN_REG);
	irq_en &= ~AIC_DMA_IRQ_MASK(channel);
	irq_en |= AIC_DMA_IRQ_ALL_TASK << AIC_DMA_IRQ_SHIFT(channel);
	sys_write32(irq_en, config->base + AIC_DMA_IRQ_EN_REG);

	return 0;
}

static int artinchip_dma_stop_channel(const struct device *dev, uint32_t channel)
{
	const struct artinchip_dma_config *config = dev->config;
	uintptr_t ch_base = artinchip_dma_channel_base(config, channel);
	uint32_t irq_en;

	sys_write32(0U, ch_base + AIC_DMA_CH_EN_REG);

	irq_en = sys_read32(config->base + AIC_DMA_IRQ_EN_REG);
	irq_en &= ~AIC_DMA_IRQ_MASK(channel);
	sys_write32(irq_en, config->base + AIC_DMA_IRQ_EN_REG);

	return 0;
}

static int artinchip_dma_config(const struct device *dev, uint32_t channel, struct dma_config *cfg)
{
	const struct artinchip_dma_config *config = dev->config;
	struct artinchip_dma_data *data = dev->data;
	struct artinchip_dma_channel *ch_data;
	struct dma_block_config *block;
	uint32_t slave_id;
	bool mem_to_dev;

	if (channel >= config->channel_count) {
		return -EINVAL;
	}

	if (cfg->block_count != 1U || cfg->head_block == NULL) {
		return -ENOTSUP;
	}

	block = cfg->head_block;
	ch_data = &data->channels[channel];
	slave_id = cfg->dma_slot & 0x3FU;
	ch_data->cb = cfg->dma_callback;
	ch_data->user_data = cfg->user_data;

	switch (cfg->channel_direction) {
	case MEMORY_TO_PERIPHERAL:
		mem_to_dev = true;
		ch_data->task.src = block->source_address;
		ch_data->task.dst = block->dest_address;
		break;
	case PERIPHERAL_TO_MEMORY:
		mem_to_dev = false;
		ch_data->task.src = block->source_address;
		ch_data->task.dst = block->dest_address;
		break;
	default:
		return -ENOTSUP;
	}

	ch_data->task.len = block->block_size;
	ch_data->task.delay = AIC_DMA_TASK_DELAY;
	ch_data->task.p_next = AIC_DMA_LINK_END_FLAG;
	ch_data->task.v_next = NULL;
	ch_data->task.cfg = artinchip_dma_build_task_cfg(slave_id, mem_to_dev);
	ch_data->task.mode = mem_to_dev ? AIC_DMA_MODE_MEM_TO_DEV : AIC_DMA_MODE_DEV_TO_MEM;
	ch_data->active = true;

	return 0;
}

static int artinchip_dma_start(const struct device *dev, uint32_t channel)
{
	const struct artinchip_dma_config *config = dev->config;
	struct artinchip_dma_data *data = dev->data;

	if (channel >= config->channel_count) {
		return -EINVAL;
	}

	return artinchip_dma_start_transfer(dev, channel, &data->channels[channel].task);
}

static int artinchip_dma_stop(const struct device *dev, uint32_t channel)
{
	const struct artinchip_dma_config *config = dev->config;
	struct artinchip_dma_data *data = dev->data;

	if (channel >= config->channel_count) {
		return -EINVAL;
	}

	data->channels[channel].active = false;

	return artinchip_dma_stop_channel(dev, channel);
}

static int artinchip_dma_get_status(const struct device *dev, uint32_t channel, struct dma_status *stat)
{
	const struct artinchip_dma_config *config = dev->config;
	struct artinchip_dma_data *data = dev->data;
	uintptr_t ch_base;

	if (channel >= config->channel_count) {
		return -EINVAL;
	}

	ch_base = artinchip_dma_channel_base(config, channel);
	stat->busy = data->channels[channel].active;
	stat->pending_length = sys_read32(ch_base + AIC_DMA_CH_LEFT_REG);
	stat->dir = MEMORY_TO_PERIPHERAL;

	return 0;
}

static int artinchip_dma_reload(const struct device *dev, uint32_t channel, uint32_t src, uint32_t dst,
			   size_t size)
{
	const struct artinchip_dma_config *config = dev->config;
	struct artinchip_dma_data *data = dev->data;
	struct artinchip_dma_channel *ch_data;

	if (channel >= config->channel_count) {
		return -EINVAL;
	}

	ch_data = &data->channels[channel];
	ch_data->task.src = src;
	ch_data->task.dst = dst;
	ch_data->task.len = size;

	return 0;
}

static void artinchip_dma_isr(const struct device *dev)
{
	const struct artinchip_dma_config *config = dev->config;
	struct artinchip_dma_data *data = dev->data;
	uint32_t status = sys_read32(config->base + AIC_DMA_IRQ_STA_REG);
	uint32_t channel;

	if (status == 0U) {
		return;
	}

	sys_write32(status, config->base + AIC_DMA_IRQ_STA_REG);

	for (channel = 0U; channel < config->channel_count; channel++) {
		struct artinchip_dma_channel *ch_data = &data->channels[channel];

		if ((status & AIC_DMA_IRQ_MASK(channel)) == 0U) {
			continue;
		}

		(void)artinchip_dma_stop_channel(dev, channel);
		ch_data->active = false;

		if (ch_data->cb != NULL) {
			ch_data->cb(dev, ch_data->user_data, channel, 0);
		}
	}
}

static int artinchip_dma_init(const struct device *dev)
{
	const struct artinchip_dma_config *config = dev->config;
	struct artinchip_dma_data *data = dev->data;

	sys_write32(0U, config->base + AIC_DMA_IRQ_EN_REG);
	data->dma_ctx.magic = DMA_MAGIC;
	data->dma_ctx.dma_channels = config->channel_count;
	data->dma_ctx.atomic = data->channels_atomic;
	config->irq_config(dev);

	return 0;
}

static DEVICE_API(dma, artinchip_dma_api) = {
	.config = artinchip_dma_config,
	.start = artinchip_dma_start,
	.stop = artinchip_dma_stop,
	.get_status = artinchip_dma_get_status,
	.reload = artinchip_dma_reload,
};

#define AIC_DMA_IRQ_CFG(n)                                                                        \
	static void artinchip_dma_irq_config_##n(const struct device *dev)                             \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), artinchip_dma_isr,               \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                         \
	}

#define AIC_DMA_INIT(n)                                                                           \
	AIC_DMA_IRQ_CFG(n)                                                                        \
	static const struct artinchip_dma_config artinchip_dma_config_##n = {                                \
		.base = DT_INST_REG_ADDR(n),                                                      \
		.channel_count = DT_INST_PROP(n, dma_channels),                                   \
		.irq_config = artinchip_dma_irq_config_##n,                                            \
	};                                                                                         \
	static struct artinchip_dma_data artinchip_dma_data_##n;                                             \
	DEVICE_DT_INST_DEFINE(n, artinchip_dma_init, NULL, &artinchip_dma_data_##n, &artinchip_dma_config_##n,    \
			      PRE_KERNEL_1, CONFIG_DMA_INIT_PRIORITY, &artinchip_dma_api);

DT_INST_FOREACH_STATUS_OKAY(AIC_DMA_INIT)
