/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_rctl

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/dt-bindings/reset/artinchip-reset.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_D12P_CMU_WRITE_AUTH 0x0fe8U
#define AIC_D12P_CMU_AUTH_KEY   0xa1c00000U
#define AIC_D12P_CMU_AUTH_GRANT BIT(16)

#define AIC_UART_RESET_COUNT    8U
#define AIC_UART_RESET_REG_BASE 0x0840U
#define AIC_UART_RESET_N        BIT(13)
#define AIC_QSPI_RESET_REG_BASE 0x0460U
#define AIC_I2C_RESET_REG_BASE  0x0960U
#define AIC_PWM_RESET_REG       0x0990U
#define AIC_XSPI_RESET_REG      0x045CU
#define AIC_SYSCFG_RESET_REG    0x0800U
#define AIC_RTC_RESET_REG       0x0908U
#define AIC_TSEN_RESET_REG      0x09ACU
#define AIC_RTP_RESET_REG       0x09A8U
#define AIC_SDMC_RESET_REG_BASE    0x0470U /* d13x */
#define AIC_V1_SDMC_RESET_REG_BASE 0x0480U /* d12p */
#define AIC_SPIENC_RESET_REG    0x0810U
#define AIC_WDT_RESET_REG       0x020CU
#define AIC_SID_RESET_REG       0x0904U
#define AIC_CAN_RESET_REG_BASE  0x0980U
#define AIC_ADCIM_RESET_REG     0x09A0U
#define AIC_GPAI_RESET_REG      0x09A4U
#define AIC_PSADC_RESET_REG     0x0818U
#define AIC_CIR_RESET_REG       0x09B0U
#define AIC_MTOP_RESET_REG      0x081CU
#define AIC_PBUS_RESET_REG      0x04A0U
#define AIC_PWMCS_RESET_REG     0x0814U
#define AIC_I2S0_RESET_REG      0x0820U
#define AIC_CODEC_RESET_REG     0x0830U
#define AIC_GMAC0_RESET_REG     0x0440U
#define AIC_CE_RESET_REG        0x0418U
#define AIC_GE_RESET_REG        0x08C4U
#define AIC_VE_RESET_REG        0x08C8U
#define AIC_MIPIDSI_RESET_REG   0x0888U
#define AIC_LVDS_RESET_REG      0x0884U
#define AIC_RGB_RESET_REG       0x0880U
#define AIC_DE_RESET_REG        0x08C0U
#define AIC_DVP_RESET_REG       0x0890U
#define AIC_USBD_RESET_REG      0x041CU
#define AIC_USBH0_RESET_REG     0x0420U
#define AIC_USB_PHY0_RESET_REG  0x0430U

struct artinchip_reset_config {
	uintptr_t base;
	uint32_t reset_bit;
	bool is_d12p;
	bool has_gpio_reset;
};

struct artinchip_reset_data {
	struct k_spinlock lock;
};

static bool artinchip_reset_is_uart(uint32_t id)
{
	return id <= ARTINCHIP_RESET_UART7;
}

static bool artinchip_reset_is_qspi(uint32_t id)
{
	return (id >= ARTINCHIP_RESET_QSPI0) && (id <= ARTINCHIP_RESET_QSPI3);
}

static bool artinchip_reset_is_i2c(uint32_t id)
{
	return (id >= ARTINCHIP_RESET_I2C0) && (id <= ARTINCHIP_RESET_I2C2);
}

static bool artinchip_reset_is_sdmc(uint32_t id)
{
	return (id >= ARTINCHIP_RESET_SDMC0) && (id <= ARTINCHIP_RESET_SDMC1);
}

static bool artinchip_reset_is_can(uint32_t id)
{
	return (id >= ARTINCHIP_RESET_CAN0) && (id <= ARTINCHIP_RESET_CAN1);
}

static int artinchip_reset_reg_offset(const struct artinchip_reset_config *config, uint32_t id,
				      uint32_t *offset)
{
	if (id == ARTINCHIP_RESET_GPIO && config->has_gpio_reset) {
		*offset = 0x083cU;
		return 0;
	}
	if (artinchip_reset_is_uart(id)) {
		*offset = AIC_UART_RESET_REG_BASE + (id * 4U);
		return 0;
	}

	if (artinchip_reset_is_qspi(id)) {
		*offset = AIC_QSPI_RESET_REG_BASE +
			  ((id - ARTINCHIP_RESET_QSPI0) * 4U);
		return 0;
	}

	if (artinchip_reset_is_i2c(id)) {
		*offset = AIC_I2C_RESET_REG_BASE +
			  ((id - ARTINCHIP_RESET_I2C0) * 4U);
		return 0;
	}

	if (id == ARTINCHIP_RESET_PWM) {
		*offset = AIC_PWM_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_XSPI) {
		*offset = AIC_XSPI_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_SYSCFG) {
		*offset = AIC_SYSCFG_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_RTC) {
		*offset = AIC_RTC_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_TSEN) {
		*offset = AIC_TSEN_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_RTP) {
		*offset = AIC_RTP_RESET_REG;
		return 0;
	}

	if (artinchip_reset_is_sdmc(id)) {
		uint32_t base = config->is_d12p ? AIC_V1_SDMC_RESET_REG_BASE
						: AIC_SDMC_RESET_REG_BASE;

		*offset = base + ((id - ARTINCHIP_RESET_SDMC0) * 4U);
		return 0;
	}

	if (id == ARTINCHIP_RESET_SPIENC) {
		*offset = AIC_SPIENC_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_WDT) {
		*offset = AIC_WDT_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_SID) {
		*offset = AIC_SID_RESET_REG;
		return 0;
	}

	if (artinchip_reset_is_can(id)) {
		*offset = AIC_CAN_RESET_REG_BASE + ((id - ARTINCHIP_RESET_CAN0) * 4U);
		return 0;
	}

	if (id == ARTINCHIP_RESET_ADCIM) {
		*offset = AIC_ADCIM_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_GPAI) {
		*offset = AIC_GPAI_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_PSADC) {
		*offset = AIC_PSADC_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_CIR) {
		*offset = AIC_CIR_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_MTOP) {
		*offset = AIC_MTOP_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_PBUS) {
		*offset = AIC_PBUS_RESET_REG;
		return 0;
	}

	if (id == ARTINCHIP_RESET_PWMCS) {
		*offset = AIC_PWMCS_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_I2S0) {
		*offset = AIC_I2S0_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_CODEC) {
		*offset = AIC_CODEC_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_GMAC0) {
		*offset = AIC_GMAC0_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_CE) {
		*offset = AIC_CE_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_GE) {
		*offset = AIC_GE_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_VE) {
		*offset = AIC_VE_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_MIPIDSI) {
		*offset = AIC_MIPIDSI_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_LVDS) {
		*offset = AIC_LVDS_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_RGB) {
		*offset = AIC_RGB_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_DE) {
		*offset = AIC_DE_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_DVP) {
		*offset = AIC_DVP_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_USBD) {
		*offset = AIC_USBD_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_USBH0) {
		*offset = AIC_USBH0_RESET_REG;
		return 0;
	}
	if (id == ARTINCHIP_RESET_USB_PHY0) {
		*offset = AIC_USB_PHY0_RESET_REG;
		return 0;
	}

	return -EINVAL;
}

static int artinchip_reset_update(const struct device *dev, uint32_t id, bool assert)
{
	const struct artinchip_reset_config *config = dev->config;
	struct artinchip_reset_data *data = dev->data;
	uint32_t offset;
	uint32_t value;
	int ret = 0;

	if (artinchip_reset_reg_offset(config, id, &offset) != 0) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + offset);
		value = assert ? (value & ~config->reset_bit) : (value | config->reset_bit);
		if (config->is_d12p) {
			/* D12P CMU v3.2 grants one write to the requested register. */
			sys_write32(AIC_D12P_CMU_AUTH_KEY | offset,
				    config->base + AIC_D12P_CMU_WRITE_AUTH);
			ret = -ETIMEDOUT;
			for (unsigned int i = 0; i < 100U; i++) {
				if (sys_read32(config->base + AIC_D12P_CMU_WRITE_AUTH) &
				    AIC_D12P_CMU_AUTH_GRANT) {
					ret = 0;
					break;
				}
				k_busy_wait(10);
			}
		}
		if (ret == 0) {
			sys_write32(value, config->base + offset);
			if (config->is_d12p &&
			    ((sys_read32(config->base + offset) ^ value) & config->reset_bit)) {
				ret = -EIO;
			}
		}
	}

	return ret;
}

static int artinchip_reset_status(const struct device *dev, uint32_t id, uint8_t *status)
{
	const struct artinchip_reset_config *config = dev->config;
	uint32_t offset;

	if (artinchip_reset_reg_offset(config, id, &offset) != 0) {
		return -EINVAL;
	}

	*status = (sys_read32(config->base + offset) & config->reset_bit) == 0U;

	return 0;
}

static int artinchip_reset_assert(const struct device *dev, uint32_t id)
{
	return artinchip_reset_update(dev, id, true);
}

static int artinchip_reset_deassert(const struct device *dev, uint32_t id)
{
	return artinchip_reset_update(dev, id, false);
}

static int artinchip_reset_toggle(const struct device *dev, uint32_t id)
{
	int ret;

	ret = artinchip_reset_assert(dev, id);
	if (ret != 0) {
		return ret;
	}

	k_busy_wait(30);

	ret = artinchip_reset_deassert(dev, id);
	if (ret != 0) {
		return ret;
	}

	k_busy_wait(30);

	return 0;
}

static DEVICE_API(reset, artinchip_reset_api) = {
	.status = artinchip_reset_status,
	.line_assert = artinchip_reset_assert,
	.line_deassert = artinchip_reset_deassert,
	.line_toggle = artinchip_reset_toggle,
};

static const struct artinchip_reset_config artinchip_reset_config = {
	.base = DT_REG_ADDR(DT_INST_PARENT(0)),
	.reset_bit = COND_CODE_1(DT_INST_NODE_HAS_COMPAT(0, artinchip_d12p_rctl),
				 (BIT(24)), (AIC_UART_RESET_N)),
	.has_gpio_reset = DT_INST_NODE_HAS_COMPAT(0, artinchip_d12p_rctl) ||
		DT_INST_NODE_HAS_COMPAT(0, artinchip_d21x_rctl),
	.is_d12p = DT_INST_NODE_HAS_COMPAT(0, artinchip_d12p_rctl),
};

static struct artinchip_reset_data artinchip_reset_data;

DEVICE_DT_INST_DEFINE(0, NULL, NULL, &artinchip_reset_data, &artinchip_reset_config, PRE_KERNEL_1,
		      CONFIG_RESET_INIT_PRIORITY, &artinchip_reset_api);
