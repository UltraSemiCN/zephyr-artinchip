/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_gpio

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_GPIO_INPUT_REG       0x00U
#define AIC_GPIO_OUTPUT_REG      0x04U
#define AIC_GPIO_IRQ_ENABLE_REG  0x08U
#define AIC_GPIO_IRQ_STATUS_REG  0x0CU
#define AIC_GPIO_OUTPUT_CLR_REG  0x10U
#define AIC_GPIO_OUTPUT_SET_REG  0x14U
#define AIC_GPIO_OUTPUT_TOG_REG  0x18U
#define AIC_GPIO_PIN_CFG_BASE    0x80U
#define AIC_GPIO_PIN_CFG_STRIDE  0x04U

#define AIC_GPIO_FUNC_MASK       GENMASK(3, 0)
#define AIC_GPIO_FUNC_GPIO       1U
#define AIC_GPIO_PULL_MASK       GENMASK(9, 8)
#define AIC_GPIO_PULL_NONE       0U
#define AIC_GPIO_PULL_DOWN       2U
#define AIC_GPIO_PULL_UP         3U
#define AIC_GPIO_IRQ_MODE_MASK   GENMASK(14, 12)
#define AIC_GPIO_IRQ_FALLING     0U
#define AIC_GPIO_IRQ_RISING      1U
#define AIC_GPIO_IRQ_LEVEL_LOW   2U
#define AIC_GPIO_IRQ_LEVEL_HIGH  3U
#define AIC_GPIO_IRQ_BOTH        4U
#define AIC_GPIO_DIRECTION_MASK  GENMASK(17, 16)
#define AIC_GPIO_DIRECTION_INPUT 1U
#define AIC_GPIO_DIRECTION_OUTPUT 2U

struct artinchip_gpio_config {
	struct gpio_driver_config common;
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	void (*irq_config_func)(const struct device *dev);
};

struct artinchip_gpio_data {
	struct gpio_driver_data common;
	sys_slist_t callbacks;
	struct k_spinlock lock;
};

static uintptr_t artinchip_gpio_pin_cfg_reg(const struct artinchip_gpio_config *config, gpio_pin_t pin)
{
	return config->base + AIC_GPIO_PIN_CFG_BASE + pin * AIC_GPIO_PIN_CFG_STRIDE;
}

static int artinchip_gpio_pin_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct artinchip_gpio_config *config = dev->config;
	struct artinchip_gpio_data *data = dev->data;
	uint32_t direction;
	uint32_t pull;
	uint32_t value;

	if ((flags & GPIO_SINGLE_ENDED) != 0U) {
		return -ENOTSUP;
	}

	switch (flags & GPIO_DIR_MASK) {
	case GPIO_INPUT:
		direction = AIC_GPIO_DIRECTION_INPUT;
		break;
	case GPIO_OUTPUT:
		direction = AIC_GPIO_DIRECTION_OUTPUT;
		break;
	default:
		return -ENOTSUP;
	}

	if ((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) == (GPIO_PULL_UP | GPIO_PULL_DOWN)) {
		return -EINVAL;
	}

	if ((flags & GPIO_PULL_UP) != 0U) {
		pull = AIC_GPIO_PULL_UP;
	} else if ((flags & GPIO_PULL_DOWN) != 0U) {
		pull = AIC_GPIO_PULL_DOWN;
	} else {
		pull = AIC_GPIO_PULL_NONE;
	}

	if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
		sys_write32(BIT(pin), config->base + AIC_GPIO_OUTPUT_SET_REG);
	} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
		sys_write32(BIT(pin), config->base + AIC_GPIO_OUTPUT_CLR_REG);
	}

	K_SPINLOCK(&data->lock) {
		value = sys_read32(artinchip_gpio_pin_cfg_reg(config, pin));
		value &= ~(AIC_GPIO_FUNC_MASK | AIC_GPIO_PULL_MASK | AIC_GPIO_DIRECTION_MASK);
		value |= FIELD_PREP(AIC_GPIO_FUNC_MASK, AIC_GPIO_FUNC_GPIO);
		value |= FIELD_PREP(AIC_GPIO_PULL_MASK, pull);
		value |= FIELD_PREP(AIC_GPIO_DIRECTION_MASK, direction);
		sys_write32(value, artinchip_gpio_pin_cfg_reg(config, pin));
	}

	return 0;
}

static int artinchip_gpio_port_get_raw(const struct device *dev, gpio_port_value_t *value)
{
	const struct artinchip_gpio_config *config = dev->config;

	*value = sys_read32(config->base + AIC_GPIO_INPUT_REG) & config->common.port_pin_mask;

	return 0;
}

static int artinchip_gpio_port_set_masked_raw(const struct device *dev, gpio_port_pins_t mask,
					 gpio_port_value_t value)
{
	const struct artinchip_gpio_config *config = dev->config;
	struct artinchip_gpio_data *data = dev->data;
	uint32_t output;

	mask &= config->common.port_pin_mask;

	K_SPINLOCK(&data->lock) {
		output = sys_read32(config->base + AIC_GPIO_OUTPUT_REG);
		output = (output & ~mask) | (value & mask);
		sys_write32(output, config->base + AIC_GPIO_OUTPUT_REG);
	}

	return 0;
}

static int artinchip_gpio_port_set_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	const struct artinchip_gpio_config *config = dev->config;

	sys_write32(pins & config->common.port_pin_mask, config->base + AIC_GPIO_OUTPUT_SET_REG);

	return 0;
}

static int artinchip_gpio_port_clear_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	const struct artinchip_gpio_config *config = dev->config;

	sys_write32(pins & config->common.port_pin_mask, config->base + AIC_GPIO_OUTPUT_CLR_REG);

	return 0;
}

static int artinchip_gpio_port_toggle_bits(const struct device *dev, gpio_port_pins_t pins)
{
	const struct artinchip_gpio_config *config = dev->config;

	sys_write32(pins & config->common.port_pin_mask, config->base + AIC_GPIO_OUTPUT_TOG_REG);

	return 0;
}

static int artinchip_gpio_pin_interrupt_configure(const struct device *dev, gpio_pin_t pin,
					     enum gpio_int_mode mode,
					     enum gpio_int_trig trig)
{
	const struct artinchip_gpio_config *config = dev->config;
	struct artinchip_gpio_data *data = dev->data;
	uint32_t irq_mode;
	uint32_t enabled;
	uint32_t value;

	if (mode == GPIO_INT_MODE_DISABLED) {
		K_SPINLOCK(&data->lock) {
			enabled = sys_read32(config->base + AIC_GPIO_IRQ_ENABLE_REG);
			sys_write32(enabled & ~BIT(pin), config->base + AIC_GPIO_IRQ_ENABLE_REG);
			sys_write32(BIT(pin), config->base + AIC_GPIO_IRQ_STATUS_REG);
		}

		return 0;
	}

	if (mode == GPIO_INT_MODE_EDGE) {
		switch (trig) {
		case GPIO_INT_TRIG_LOW:
			irq_mode = AIC_GPIO_IRQ_FALLING;
			break;
		case GPIO_INT_TRIG_HIGH:
			irq_mode = AIC_GPIO_IRQ_RISING;
			break;
		case GPIO_INT_TRIG_BOTH:
			irq_mode = AIC_GPIO_IRQ_BOTH;
			break;
		default:
			return -ENOTSUP;
		}
	} else if (mode == GPIO_INT_MODE_LEVEL) {
		if (trig == GPIO_INT_TRIG_LOW) {
			irq_mode = AIC_GPIO_IRQ_LEVEL_LOW;
		} else if (trig == GPIO_INT_TRIG_HIGH) {
			irq_mode = AIC_GPIO_IRQ_LEVEL_HIGH;
		} else {
			return -ENOTSUP;
		}
	} else {
		return -ENOTSUP;
	}

	K_SPINLOCK(&data->lock) {
		enabled = sys_read32(config->base + AIC_GPIO_IRQ_ENABLE_REG);
		sys_write32(enabled & ~BIT(pin), config->base + AIC_GPIO_IRQ_ENABLE_REG);

		value = sys_read32(artinchip_gpio_pin_cfg_reg(config, pin));
		value &= ~AIC_GPIO_IRQ_MODE_MASK;
		value |= FIELD_PREP(AIC_GPIO_IRQ_MODE_MASK, irq_mode);
		sys_write32(value, artinchip_gpio_pin_cfg_reg(config, pin));

		sys_write32(BIT(pin), config->base + AIC_GPIO_IRQ_STATUS_REG);
		sys_write32(enabled | BIT(pin), config->base + AIC_GPIO_IRQ_ENABLE_REG);
	}

	return 0;
}

static int artinchip_gpio_manage_callback(const struct device *dev, struct gpio_callback *callback,
				     bool set)
{
	struct artinchip_gpio_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

static void artinchip_gpio_isr(const struct device *dev)
{
	const struct artinchip_gpio_config *config = dev->config;
	struct artinchip_gpio_data *data = dev->data;
	uint32_t pending;

	pending = sys_read32(config->base + AIC_GPIO_IRQ_STATUS_REG);
	pending &= sys_read32(config->base + AIC_GPIO_IRQ_ENABLE_REG);
	pending &= config->common.port_pin_mask;
	sys_write32(pending, config->base + AIC_GPIO_IRQ_STATUS_REG);

	gpio_fire_callbacks(&data->callbacks, dev, pending);
}

static int artinchip_gpio_init(const struct device *dev)
{
	const struct artinchip_gpio_config *config = dev->config;
	int ret;

	if (!device_is_ready(config->clock_dev)) {
		return -ENODEV;
	}

	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0) {
		return ret;
	}

	sys_write32(0U, config->base + AIC_GPIO_IRQ_ENABLE_REG);
	sys_write32(config->common.port_pin_mask, config->base + AIC_GPIO_IRQ_STATUS_REG);
	config->irq_config_func(dev);

	return 0;
}

static DEVICE_API(gpio, artinchip_gpio_api) = {
	.pin_configure = artinchip_gpio_pin_configure,
	.port_get_raw = artinchip_gpio_port_get_raw,
	.port_set_masked_raw = artinchip_gpio_port_set_masked_raw,
	.port_set_bits_raw = artinchip_gpio_port_set_bits_raw,
	.port_clear_bits_raw = artinchip_gpio_port_clear_bits_raw,
	.port_toggle_bits = artinchip_gpio_port_toggle_bits,
	.pin_interrupt_configure = artinchip_gpio_pin_interrupt_configure,
	.manage_callback = artinchip_gpio_manage_callback,
};

#define AIC_GPIO_INIT(inst)									\
	static void artinchip_gpio_irq_config_##inst(const struct device *dev)			\
	{											\
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority), artinchip_gpio_isr,	\
			    DEVICE_DT_INST_GET(inst), 0);					\
		irq_enable(DT_INST_IRQN(inst));							\
	}											\
												\
	static const struct artinchip_gpio_config artinchip_gpio_config_##inst = {			\
		.common = GPIO_COMMON_CONFIG_FROM_DT_INST(inst),					\
		.base = DT_INST_REG_ADDR(inst),							\
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),				\
		.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),		\
		.irq_config_func = artinchip_gpio_irq_config_##inst,					\
	};											\
												\
	static struct artinchip_gpio_data artinchip_gpio_data_##inst;					\
												\
	DEVICE_DT_INST_DEFINE(inst, artinchip_gpio_init, NULL, &artinchip_gpio_data_##inst,		\
			      &artinchip_gpio_config_##inst, POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY,	\
			      &artinchip_gpio_api);

DT_INST_FOREACH_STATUS_OKAY(AIC_GPIO_INIT)
