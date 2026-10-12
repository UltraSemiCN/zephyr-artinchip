/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT artinchip_d13x_axicfg

#include <zephyr/device.h>
#include <zephyr/drivers/misc/artinchip_axicfg.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_AXICFG_DIRECTION_STRIDE 0x10U
#define AIC_AXICFG_PORT_STRIDE      0x20U
#define AIC_AXICFG_CFG1_OFFSET      0x04U

#define AIC_AXICFG_REGULATOR_ENABLE BIT(0)
#define AIC_AXICFG_SLAVE_READY      BIT(1)
#define AIC_AXICFG_BURST_LIMIT      GENMASK(15, 8)
#define AIC_AXICFG_INTERNAL_QOS     BIT(16)
#define AIC_AXICFG_QOS              GENMASK(27, 24)
#define AIC_AXICFG_BASIC_RATE       GENMASK(11, 0)
#define AIC_AXICFG_BURST_RATE       GENMASK(27, 16)
#define AIC_AXICFG_CFG0_FIELDS      (AIC_AXICFG_REGULATOR_ENABLE | AIC_AXICFG_SLAVE_READY | \
				      AIC_AXICFG_BURST_LIMIT | AIC_AXICFG_INTERNAL_QOS | \
				      AIC_AXICFG_QOS)
#define AIC_AXICFG_CFG1_FIELDS      (AIC_AXICFG_BASIC_RATE | AIC_AXICFG_BURST_RATE)

struct artinchip_axicfg_config {
	uintptr_t base;
};

struct artinchip_axicfg_data {
	struct k_mutex lock;
};

static int artinchip_axicfg_offsets(enum artinchip_axicfg_port port,
			       enum artinchip_axicfg_direction direction, uintptr_t *cfg0,
			       uintptr_t *cfg1)
{
	uintptr_t offset;

	if (((uint32_t)port >= ARTINCHIP_AXICFG_PORT_COUNT) ||
	    ((uint32_t)direction > ARTINCHIP_AXICFG_WRITE)) {
		return -EINVAL;
	}

	offset = (uintptr_t)direction * AIC_AXICFG_DIRECTION_STRIDE +
		 (uintptr_t)port * AIC_AXICFG_PORT_STRIDE;
	*cfg0 = offset;
	*cfg1 = offset + AIC_AXICFG_CFG1_OFFSET;
	return 0;
}

int artinchip_axicfg_configure(const struct device *dev, enum artinchip_axicfg_port port,
			       enum artinchip_axicfg_direction direction,
			       const struct artinchip_axicfg_config *config)
{
	const struct artinchip_axicfg_config *dev_config = dev->config;
	struct artinchip_axicfg_data *data = dev->data;
	uintptr_t cfg0_offset;
	uintptr_t cfg1_offset;
	uint32_t cfg0;
	uint32_t cfg1;
	int ret;

	if ((config == NULL) || (config->qos > 0xFU) || (config->basic_rate > 0xFFFU) ||
	    (config->burst_rate > 0xFFFU)) {
		return -EINVAL;
	}

	ret = artinchip_axicfg_offsets(port, direction, &cfg0_offset, &cfg1_offset);
	if (ret != 0) {
		return ret;
	}

	cfg0 = FIELD_PREP(AIC_AXICFG_BURST_LIMIT, config->burst_limit) |
	       FIELD_PREP(AIC_AXICFG_QOS, config->qos);
	cfg0 |= config->regulator_enable ? AIC_AXICFG_REGULATOR_ENABLE : 0U;
	cfg0 |= config->slave_ready ? AIC_AXICFG_SLAVE_READY : 0U;
	cfg0 |= config->internal_qos ? AIC_AXICFG_INTERNAL_QOS : 0U;
	cfg1 = FIELD_PREP(AIC_AXICFG_BASIC_RATE, config->basic_rate) |
	       FIELD_PREP(AIC_AXICFG_BURST_RATE, config->burst_rate);

	k_mutex_lock(&data->lock, K_FOREVER);
	sys_write32((sys_read32(dev_config->base + cfg0_offset) & ~AIC_AXICFG_CFG0_FIELDS) | cfg0,
		    dev_config->base + cfg0_offset);
	sys_write32((sys_read32(dev_config->base + cfg1_offset) & ~AIC_AXICFG_CFG1_FIELDS) | cfg1,
		    dev_config->base + cfg1_offset);
	k_mutex_unlock(&data->lock);

	return 0;
}

int artinchip_axicfg_get_config(const struct device *dev, enum artinchip_axicfg_port port,
				enum artinchip_axicfg_direction direction,
				struct artinchip_axicfg_config *config)
{
	const struct artinchip_axicfg_config *dev_config = dev->config;
	struct artinchip_axicfg_data *data = dev->data;
	uintptr_t cfg0_offset;
	uintptr_t cfg1_offset;
	uint32_t cfg0;
	uint32_t cfg1;
	int ret;

	if (config == NULL) {
		return -EINVAL;
	}

	ret = artinchip_axicfg_offsets(port, direction, &cfg0_offset, &cfg1_offset);
	if (ret != 0) {
		return ret;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	cfg0 = sys_read32(dev_config->base + cfg0_offset);
	cfg1 = sys_read32(dev_config->base + cfg1_offset);
	k_mutex_unlock(&data->lock);

	config->regulator_enable = (cfg0 & AIC_AXICFG_REGULATOR_ENABLE) != 0U;
	config->slave_ready = (cfg0 & AIC_AXICFG_SLAVE_READY) != 0U;
	config->burst_limit = FIELD_GET(AIC_AXICFG_BURST_LIMIT, cfg0);
	config->internal_qos = (cfg0 & AIC_AXICFG_INTERNAL_QOS) != 0U;
	config->qos = FIELD_GET(AIC_AXICFG_QOS, cfg0);
	config->basic_rate = FIELD_GET(AIC_AXICFG_BASIC_RATE, cfg1);
	config->burst_rate = FIELD_GET(AIC_AXICFG_BURST_RATE, cfg1);

	return 0;
}

int artinchip_axicfg_set_priority(const struct device *dev, enum artinchip_axicfg_port port,
				  uint8_t priority)
{
	const struct artinchip_axicfg_config *config = dev->config;
	struct artinchip_axicfg_data *data = dev->data;
	uintptr_t cfg0_offset;
	uintptr_t unused;
	uint32_t value;
	int ret;

	if (priority > 0xFU) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	for (enum artinchip_axicfg_direction direction = ARTINCHIP_AXICFG_READ;
	     direction <= ARTINCHIP_AXICFG_WRITE; direction++) {
		ret = artinchip_axicfg_offsets(port, direction, &cfg0_offset, &unused);
		if (ret != 0) {
			k_mutex_unlock(&data->lock);
			return ret;
		}

		value = sys_read32(config->base + cfg0_offset);
		value &= ~(AIC_AXICFG_INTERNAL_QOS | AIC_AXICFG_QOS);
		value |= AIC_AXICFG_INTERNAL_QOS | FIELD_PREP(AIC_AXICFG_QOS, priority);
		sys_write32(value, config->base + cfg0_offset);
	}
	k_mutex_unlock(&data->lock);

	return 0;
}

static int artinchip_axicfg_init(const struct device *dev)
{
	struct artinchip_axicfg_data *data = dev->data;

	k_mutex_init(&data->lock);
	return 0;
}

static const struct artinchip_axicfg_config artinchip_axicfg_config = {
	.base = DT_INST_REG_ADDR(0),
};
static struct artinchip_axicfg_data artinchip_axicfg_data;

DEVICE_DT_INST_DEFINE(0, artinchip_axicfg_init, NULL, &artinchip_axicfg_data, &artinchip_axicfg_config,
		      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
