/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_cctl

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/artinchip-clock.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_UART_CLK_COUNT       8U
#define AIC_UART_CLK_REG_BASE    0x0840U
/*
 * UART module clock gate/divider layout, matching luban-lite d12p
 * (bsp/artinchip/sys/d12p/aic_clk_def.c): UART uses pre_mod_apb_gates =
 * {17, 16, 20} and mod_div1 (6-bit divider, bits [5:0]). All three gates
 * (pre, module, APB bus) must be set to enable the clock; setting only a
 * subset, or the wrong bits, leaves the UART unclocked (THRE never asserts,
 * no TX). The earlier BIT(8)/BIT(12) definitions were incorrect and only
 * appeared to work for UART0/QSPI0 because those are clocked by the
 * bootloader before Zephyr starts.
 */
#define AIC_UART_CLK_MOD_GATE    BIT(16)
#define AIC_UART_CLK_PRE_GATE    BIT(17)
#define AIC_UART_CLK_BUS_GATE    BIT(20)
#define AIC_UART_CLK_GATE_MASK   (AIC_UART_CLK_PRE_GATE | AIC_UART_CLK_MOD_GATE | \
				  AIC_UART_CLK_BUS_GATE)
#define AIC_UART_CLK_DIV_MASK    GENMASK(5, 0)
#define AIC_D13X_UART_CLK_GATE_MASK (BIT(12) | BIT(8))
#define AIC_D13X_UART_CLK_DIV_MASK GENMASK(4, 0)
#define AIC_UART_CLK_DEFAULT_HZ  48000000U
/* Legacy D13x gates; D12P uses its CMU v3.2 gate sequence below. */
#define AIC_QSPI_CLK_BUS_GATE    BIT(12)
#define AIC_QSPI_CLK_MOD_GATE    BIT(8)
#define AIC_QSPI_CLK_COUNT       4U
#define AIC_QSPI_CLK_REG_BASE    0x0460U
#define AIC_QSPI_CLK_DEFAULT_HZ  100000000U
#define AIC_GPIO_CLK_REG         0x083CU
#define AIC_GPIO_CLK_BUS_GATE    BIT(12)
#define AIC_I2C_CLK_COUNT        3U
#define AIC_I2C_CLK_REG_BASE     0x0960U
#define AIC_I2C_CLK_BUS_GATE     BIT(12)
#define AIC_V1_I2C_CLK_BUS_GATE  BIT(20)
#define AIC_APB0_CLK_DEFAULT_HZ  120000000U
#define AIC_APB1_CLK_DEFAULT_HZ  24000000U
#define AIC_PWM_CLK_REG          0x0990U
#define AIC_PWM_CLK_BUS_GATE     BIT(12)
#define AIC_PWM_CLK_MOD_GATE     BIT(8)
#define AIC_PWM_CLK_DIV_MASK     GENMASK(4, 0)
#define AIC_PWM_CLK_DEFAULT_HZ   48000000U
#define AIC_XSPI_CLK_REG         0x045CU
#define AIC_XSPI_CLK_BUS_GATE    BIT(12)
#define AIC_XSPI_CLK_MOD_GATE    BIT(8)
#define AIC_XSPI_CLK_DEFAULT_HZ  198000000U
#define AIC_SYSCFG_CLK_REG       0x0800U
#define AIC_SYSCFG_CLK_BUS_GATE  BIT(12)
#define AIC_RTC_CLK_REG          0x0908U
#define AIC_RTC_CLK_BUS_GATE     BIT(12)
#define AIC_TSEN_CLK_REG         0x09ACU
#define AIC_TSEN_CLK_BUS_GATE    BIT(12)
#define AIC_RTP_CLK_REG          0x09A8U
#define AIC_RTP_CLK_BUS_GATE     BIT(12)
#define AIC_SDMC_CLK_REG_BASE       0x0470U /* d13x SDMC0/1 */
#define AIC_V1_SDMC_CLK_REG_BASE    0x0480U /* d12p SDMC0/1 */
#define AIC_SDMC_CLK_BUS_GATE       BIT(12) /* d13x ahb/bus */
#define AIC_SDMC_CLK_MOD_GATE       BIT(8)  /* d13x mod */
/* d12p: pre_mod_ahb_gates = {17, 16, 21}, mod_div2 width=4 */
#define AIC_V1_SDMC_CLK_PRE_GATE    BIT(17)
#define AIC_V1_SDMC_CLK_MOD_GATE    BIT(16)
#define AIC_V1_SDMC_CLK_AHB_GATE    BIT(21)
#define AIC_V1_SDMC_CLK_GATE_MASK   (AIC_V1_SDMC_CLK_PRE_GATE | AIC_V1_SDMC_CLK_MOD_GATE | \
				     AIC_V1_SDMC_CLK_AHB_GATE)
#define AIC_SDMC_CLK_DIV_MASK       GENMASK(4, 0) /* d13x 5-bit */
#define AIC_V1_SDMC_CLK_DIV_MASK    GENMASK(3, 0) /* d12p 4-bit */
#define AIC_SDMC_CLK_DEFAULT_HZ     100000000U
/* SDMC parent is PLL_FRA0 (~792 MHz on D125), not CPU clock_frequency. */
#define AIC_SDMC_CLK_PARENT_HZ      792000000U
#define AIC_SPIENC_CLK_REG       0x0810U
#define AIC_SPIENC_CLK_BUS_GATE  BIT(12)
#define AIC_SPIENC_CLK_MOD_GATE  BIT(8)
#define AIC_WDT_CLK_REG          0x020CU
#define AIC_WDT_CLK_BUS_GATE     BIT(12)
#define AIC_WDT_CLK_MOD_GATE     BIT(8)
#define AIC_SID_CLK_REG          0x0904U
#define AIC_SID_CLK_BUS_GATE     BIT(12)
#define AIC_SID_CLK_MOD_GATE     BIT(8)
#define AIC_CAN_CLK_REG_BASE     0x0980U
#define AIC_CAN_CLK_BUS_GATE     BIT(12)
#define AIC_ADCIM_CLK_REG        0x09A0U
#define AIC_ADCIM_CLK_BUS_GATE   BIT(12)
#define AIC_ADCIM_CLK_MOD_GATE   BIT(8)
#define AIC_ADCIM_CLK_DIV_MASK   GENMASK(4, 0)
#define AIC_ADCIM_CLK_DEFAULT_HZ 48000000U
#define AIC_GPAI_CLK_REG         0x09A4U
#define AIC_GPAI_CLK_BUS_GATE    BIT(12)
#define AIC_PSADC_CLK_REG        0x0818U
#define AIC_PSADC_CLK_BUS_GATE   BIT(12)
#define AIC_PSADC_CLK_MOD_GATE   BIT(8)
#define AIC_PSADC_CLK_DIV_MASK   GENMASK(4, 0)
#define AIC_PSADC_CLK_DEFAULT_HZ 40000000U
#define AIC_CIR_CLK_REG          0x09B0U
#define AIC_CIR_CLK_BUS_GATE     BIT(12)
#define AIC_MTOP_CLK_REG         0x081CU
#define AIC_PBUS_CLK_REG         0x04A0U
#define AIC_PWMCS_CLK_REG        0x0814U
#define AIC_SIMPLE_CLK_BUS_GATE  BIT(12)
#define AIC_PWMCS_CLK_MOD_GATE   BIT(8)
#define AIC_PWMCS_CLK_DIV_MASK   GENMASK(4, 0)
#define AIC_PWMCS_CLK_DEFAULT_HZ 200000000U
#define AIC_I2S0_CLK_REG         0x0820U
#define AIC_I2S0_CLK_GATE_MASK   (BIT(12) | BIT(8))
#define AIC_I2S0_CLK_DEFAULT_HZ  24576000U
#define AIC_CODEC_CLK_REG        0x0830U
#define AIC_CODEC_CLK_GATE_MASK  (BIT(12) | BIT(8))
#define AIC_CODEC_CLK_DEFAULT_HZ 24576000U
#define AIC_GMAC0_CLK_REG        0x0440U
#define AIC_GMAC0_CLK_GATE_MASK  (BIT(12) | BIT(8))
#define AIC_GMAC0_CLK_DEFAULT_HZ 100000000U
#define AIC_CE_CLK_REG           0x0418U
#define AIC_CE_CLK_GATE_MASK     (BIT(12) | BIT(8))
#define AIC_CE_CLK_DEFAULT_HZ    200000000U
#define AIC_GE_CLK_REG           0x08C4U
#define AIC_GE_CLK_GATE_MASK     (BIT(12) | BIT(8))
#define AIC_GE_CLK_DEFAULT_HZ    200000000U
#define AIC_VE_CLK_REG           0x08C8U
#define AIC_VE_CLK_GATE_MASK     (BIT(12) | BIT(8))
#define AIC_VE_CLK_DEFAULT_HZ    200000000U
#define AIC_MIPIDSI_CLK_REG      0x0888U
#define AIC_MIPIDSI_CLK_GATE_MASK (BIT(12) | BIT(8))
#define AIC_MIPIDSI_CLK_DEFAULT_HZ 24000000U
#define AIC_LVDS_CLK_REG         0x0884U
#define AIC_LVDS_CLK_GATE_MASK   (BIT(12) | BIT(8))
#define AIC_RGB_CLK_REG          0x0880U
#define AIC_RGB_CLK_GATE_MASK    (BIT(12) | BIT(8))
#define AIC_DE_CLK_REG           0x08C0U
#define AIC_DE_CLK_GATE_MASK     (BIT(12) | BIT(8))
#define AIC_DE_CLK_DEFAULT_HZ    200000000U
#define AIC_DVP_CLK_REG          0x0890U
#define AIC_DVP_CLK_GATE_MASK    (BIT(12) | BIT(8))
#define AIC_DVP_CLK_DEFAULT_HZ   200000000U
#define AIC_USBD_CLK_REG         0x041CU
#define AIC_USBD_CLK_GATE_MASK   BIT(12)
#define AIC_USBH0_CLK_REG        0x0420U
#define AIC_USBH0_CLK_GATE_MASK  BIT(12)
#define AIC_USB_PHY0_CLK_REG     0x0430U
#define AIC_USB_PHY0_CLK_GATE_MASK BIT(8)
#define AIC_USB_BUS_CLK_DEFAULT_HZ 200000000U
#define AIC_USB_PHY_CLK_DEFAULT_HZ 24000000U

#define AIC_D13X_DE_MAX_HZ 150000000U
#define AIC_D13X_DE_DIV_MASK GENMASK(4, 0)
#define AIC_D13X_DISP_DIV_MASK (GENMASK(2, 0) | GENMASK(8, 4) | GENMASK(13, 10))
#define AIC_D13X_DISP_PIX_DIV_MASK GENMASK(8, 4)
#define AIC_V1_DISP_SCLK_REG          0x0220U
#define AIC_V1_DISP_PIXCLK_REG        0x0224U
#define AIC_V1_DISP_SCLK_GATE         BIT(17)
#define AIC_V1_DISP_SCLK_DIV_MASK     GENMASK(5, 0)
#define AIC_V1_DISP_PIXCLK_DIV_MASK   GENMASK(7, 0)
#define AIC_V1_MIPIDSI_DIV_MASK       GENMASK(2, 0)
#define AIC_V1_DISPLAY_SCLK_DEFAULT_HZ 104000000U
#define AIC_V1_DISPLAY_PIXCLK_DEFAULT_HZ 52000000U
#define AIC_V1_PLL_DISP_FREQ_MIN_HZ   360000000U
#define AIC_V1_PLL_FRA2_VCO_MAX_HZ    1584000000U
#define AIC_V1_OSC24M_HZ              24000000U
#define AIC_V1_PRE_MOD_APB_GATE_MASK     (BIT(17) | BIT(16) | BIT(20))
#define AIC_V1_PRE_MOD_APB_AXI_GATE_MASK (AIC_V1_PRE_MOD_APB_GATE_MASK | BIT(22))
#define AIC_V1_PLL_FRA2_GEN_REG       0x0028U
#define AIC_V1_PLL_FRA2_CFG_REG       0x0068U
#define AIC_V1_PLL_FRA2_SDM_REG       0x0088U
#define AIC_V1_PLL_EN_BIT             BIT(16)
#define AIC_V1_PLL_OUT_SYS_BIT        BIT(18)
#define AIC_V1_PLL_OUT_MUX_BIT        BIT(20)
#define AIC_V1_PLL_FACTORM_EN_BIT     BIT(19)
#define AIC_V1_PLL_FACTORN_MASK       GENMASK(15, 8)
#define AIC_V1_PLL_FACTORM_MASK       GENMASK(5, 4)
#define AIC_V1_PLL_FACTORP_MASK       BIT(0)
#define AIC_V1_PLL_FRAC_EN_BIT        BIT(20)
#define AIC_V1_PLL_FRAC_DIV_MASK      GENMASK(16, 0)
#define AIC_V1_PLL_SDM_EN_BIT         BIT(31)
#define AIC_V1_PLL_SDM_MODE_BIT       BIT(29)
#define AIC_V1_PLL_SDM_AMP_MAX        0x20000U
#define AIC_V1_PLL_SDM_SPREAD_PPM     10000U
#define AIC_V1_PLL_SDM_SPREAD_FREQ    33000U
#define AIC_V1_PLL_RATE_TOLERANCE_PCT 3U
#define AIC_V1_CMU_WR_CFG_REG         0x0FE8U
#define AIC_V1_CMU_AUTH_KEY           0xA1CU
#define AIC_V1_CMU_AUTH_OK_BIT        BIT(16)

struct artinchip_clock_config {
	uintptr_t base;
	uint32_t parent_rate;
	uint32_t default_uart_rate;
	uint32_t qspi_rate;
	uint32_t apb0_rate;
	uint32_t apb1_rate;
	uint32_t pwm_rate;
	uint32_t xspi_rate;
	uint32_t display_sclk_rate;
	uint32_t display_pixclk_rate;
	bool is_d12p;
	bool is_d21x;
};

struct artinchip_clock_data {
	struct k_spinlock lock;
	uint32_t uart_rates[AIC_UART_CLK_COUNT];
	uint32_t display_pll_rate;
	uint32_t display_effective_sclk_rate;
	uint32_t display_effective_pixclk_rate;
};

static bool artinchip_clock_is_uart(uint32_t id)
{
	return (id >= ARTINCHIP_CLK_UART0) && (id <= ARTINCHIP_CLK_UART7);
}

static bool artinchip_clock_is_qspi(uint32_t id)
{
	return (id >= ARTINCHIP_CLK_QSPI0) && (id <= ARTINCHIP_CLK_QSPI3);
}

static bool artinchip_clock_is_i2c(uint32_t id)
{
	return (id >= ARTINCHIP_CLK_I2C0) && (id <= ARTINCHIP_CLK_I2C2);
}

static bool artinchip_clock_is_sdmc(uint32_t id)
{
	return (id >= ARTINCHIP_CLK_SDMC0) && (id <= ARTINCHIP_CLK_SDMC1);
}

static bool artinchip_clock_is_can(uint32_t id)
{
	return (id >= ARTINCHIP_CLK_CAN0) && (id <= ARTINCHIP_CLK_CAN1);
}

static uint32_t artinchip_clock_can_reg_offset(uint32_t id)
{
	return AIC_CAN_CLK_REG_BASE + ((id - ARTINCHIP_CLK_CAN0) * 4U);
}

static int artinchip_clock_gate_can(const struct device *dev, uint32_t id, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + artinchip_clock_can_reg_offset(id));
		if (enable) {
			value |= AIC_CAN_CLK_BUS_GATE;
		} else {
			value &= ~AIC_CAN_CLK_BUS_GATE;
		}
		sys_write32(value, config->base + artinchip_clock_can_reg_offset(id));
	}

	return 0;
}

static int artinchip_clock_gate_adcim(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t divider;
	uint32_t value;

	divider = DIV_ROUND_CLOSEST(config->parent_rate, AIC_ADCIM_CLK_DEFAULT_HZ) - 1U;
	if (divider > AIC_ADCIM_CLK_DIV_MASK) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_ADCIM_CLK_REG);
		value &= ~AIC_ADCIM_CLK_DIV_MASK;
		value |= FIELD_PREP(AIC_ADCIM_CLK_DIV_MASK, divider);
		if (enable) {
			value |= AIC_ADCIM_CLK_BUS_GATE | AIC_ADCIM_CLK_MOD_GATE;
		} else {
			value &= ~(AIC_ADCIM_CLK_BUS_GATE | AIC_ADCIM_CLK_MOD_GATE);
		}
		sys_write32(value, config->base + AIC_ADCIM_CLK_REG);
	}

	return 0;
}

static int artinchip_clock_gate_gpai(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_GPAI_CLK_REG);
		if (enable) {
			value |= AIC_GPAI_CLK_BUS_GATE;
		} else {
			value &= ~AIC_GPAI_CLK_BUS_GATE;
		}
		sys_write32(value, config->base + AIC_GPAI_CLK_REG);
	}

	return 0;
}

static int artinchip_clock_gate_cir(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_CIR_CLK_REG);
		value = enable ? (value | AIC_CIR_CLK_BUS_GATE) :
				 (value & ~AIC_CIR_CLK_BUS_GATE);
		sys_write32(value, config->base + AIC_CIR_CLK_REG);
	}

	return 0;
}

static int artinchip_clock_gate_simple(const struct device *dev, uint32_t reg, uint32_t mask,
				  bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + reg);
		value = enable ? (value | mask) : (value & ~mask);
		sys_write32(value, config->base + reg);
	}

	return 0;
}

static uint32_t artinchip_clock_display_gate_mask(const struct artinchip_clock_config *config,
					     uint32_t id, uint32_t fallback)
{
	if (!config->is_d12p) {
		return fallback;
	}

	if (id == ARTINCHIP_CLK_MIPIDSI || id == ARTINCHIP_CLK_RGB) {
		return AIC_V1_PRE_MOD_APB_GATE_MASK;
	}

	if ((id == ARTINCHIP_CLK_DE) || (id == ARTINCHIP_CLK_DVP) ||
	    (id == ARTINCHIP_CLK_GE)) {
		return AIC_V1_PRE_MOD_APB_AXI_GATE_MASK;
	}

	return fallback;
}

/*
 * USB clock gate bit positions differ between SoC generations:
 *   d13x: USBD/USBH0 = bit12 (ahb), USB PHY = bit8  (mod)
 *   d12p: USBD/USBH0 = bit21 (ahb), USB PHY = bit16 (mod)
 * The fallback masks (BIT(12)/BIT(8)) are the d13x values.
 */
static uint32_t artinchip_clock_usb_gate_mask(const struct artinchip_clock_config *config,
					      uint32_t id, uint32_t fallback)
{
	if (!config->is_d12p) {
		return fallback;
	}

	if (id == ARTINCHIP_CLK_USB_PHY0) {
		return BIT(16);
	}

	/* ARTINCHIP_CLK_USBD / ARTINCHIP_CLK_USBH0 */
	return BIT(21);
}

static uint32_t artinchip_clock_read_pll_fractional(const struct device *dev,
						 uint32_t gen_offset, uint32_t cfg_offset)
{
	const struct artinchip_clock_config *config = dev->config;
	uint32_t factor_n;
	uint32_t factor_m;
	uint32_t factor_p;
	uint32_t fra_in;
	uint32_t gen;
	uint32_t cfg;
	uint64_t rate;
	uint64_t rate_fra;
	uint64_t rate_int;
	bool fra_en;

	gen = sys_read32(config->base + gen_offset);
	cfg = sys_read32(config->base + cfg_offset);

	factor_n = FIELD_GET(AIC_V1_PLL_FACTORN_MASK, gen);
	factor_m = FIELD_GET(AIC_V1_PLL_FACTORM_MASK, gen);
	factor_p = FIELD_GET(AIC_V1_PLL_FACTORP_MASK, gen);
	fra_en = (cfg & AIC_V1_PLL_FRAC_EN_BIT) != 0U;

	if (!fra_en) {
		rate = (uint64_t)AIC_V1_OSC24M_HZ / (factor_p + 1U) * (factor_n + 1U) /
		       (factor_m + 1U);
	} else {
		fra_in = cfg & AIC_V1_PLL_FRAC_DIV_MASK;
		rate_int = (uint64_t)AIC_V1_OSC24M_HZ / (factor_p + 1U) * (factor_n + 1U) /
			   (factor_m + 1U);
		rate_fra = (uint64_t)AIC_V1_OSC24M_HZ / (factor_p + 1U) * fra_in;
		rate_fra /= (uint64_t)AIC_V1_PLL_FRAC_DIV_MASK * (factor_m + 1U);
		rate = rate_int + rate_fra;
	}

	return (uint32_t)rate;
}

static uint32_t artinchip_v1_clock_read_pll_fra2_rate(const struct device *dev)
{
	return artinchip_clock_read_pll_fractional(dev, AIC_V1_PLL_FRA2_GEN_REG,
						 AIC_V1_PLL_FRA2_CFG_REG);
}

static uint32_t artinchip_d12p_qspi_parent_rate(const struct device *dev)
{
	/* D12P QSPI uses PLL_FRA0. */
	return artinchip_clock_read_pll_fractional(dev, 0x20U, 0x60U);
}

static void artinchip_v1_cmu_reg_write(const struct artinchip_clock_config *config,
				       uint32_t offset, uint32_t value)
{
	uint32_t auth;
	unsigned int i;

	if (!config->is_d12p) {
		sys_write32(value, config->base + offset);
		return;
	}

	/*
	 * D12p CMU v3.2 protects some registers (PLL, etc.): request write
	 * access via 0xFE8 before writing, matching luban aic_hal_pll_clk.c.
	 */
	auth = (AIC_V1_CMU_AUTH_KEY << 20) | offset;
	sys_write32(auth, config->base + AIC_V1_CMU_WR_CFG_REG);

	for (i = 0U; i < 100U; i++) {
		if ((sys_read32(config->base + AIC_V1_CMU_WR_CFG_REG) &
		     AIC_V1_CMU_AUTH_OK_BIT) != 0U) {
			sys_write32(value, config->base + offset);
			return;
		}
		k_busy_wait(10);
	}

	sys_write32(value, config->base + offset);
}

static void artinchip_v1_clock_pll_fra2_enable_sdm(const struct artinchip_clock_config *config,
						   uint32_t factor_n)
{
	uint32_t ppm_max;
	uint32_t sdm_amp;
	uint32_t sdm_step;
	uint32_t reg_val;

	ppm_max = 1000000U / (factor_n + 1U);
	if (ppm_max < AIC_V1_PLL_SDM_SPREAD_PPM) {
		sdm_amp = 0U;
	} else {
		sdm_amp = AIC_V1_PLL_SDM_AMP_MAX -
			  (AIC_V1_PLL_SDM_SPREAD_PPM * AIC_V1_PLL_SDM_AMP_MAX) / ppm_max;
	}

	sdm_step = (uint32_t)(((uint64_t)(AIC_V1_PLL_SDM_AMP_MAX - sdm_amp) * 2U *
			       AIC_V1_PLL_SDM_SPREAD_FREQ) /
			      AIC_V1_OSC24M_HZ);
	if (sdm_step > 511U) {
		sdm_step = 511U;
	}

	reg_val = AIC_V1_PLL_SDM_EN_BIT | (2U << 29) | (sdm_step << 20) | (3U << 17) |
		  sdm_amp;
	artinchip_v1_cmu_reg_write(config, AIC_V1_PLL_FRA2_SDM_REG, reg_val);
}

static bool artinchip_v1_clock_rate_close(uint32_t actual, uint32_t target)
{
	uint32_t delta;

	if ((target == 0U) || (actual == 0U)) {
		return false;
	}

	delta = (actual > target) ? (actual - target) : (target - actual);

	return delta <= (target / (100U / AIC_V1_PLL_RATE_TOLERANCE_PCT));
}

static void artinchip_v1_clock_pll_fra2_bypass(const struct artinchip_clock_config *config,
					       bool bypass)
{
	uint32_t value;

	value = sys_read32(config->base + AIC_V1_PLL_FRA2_GEN_REG);
	value &= ~AIC_V1_PLL_OUT_MUX_BIT;
	if (!bypass) {
		value |= AIC_V1_PLL_OUT_MUX_BIT;
	}
	artinchip_v1_cmu_reg_write(config, AIC_V1_PLL_FRA2_GEN_REG, value);
}

static int artinchip_v1_clock_set_pll_fra2_rate(const struct device *dev, uint32_t rate)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t factor_n;
	uint32_t factor_m;
	uint32_t factor_p;
	uint32_t gen;
	uint32_t cfg;
	uint32_t remainder;
	uint32_t vco_rate;
	uint64_t fra_in = 0U;
	bool fra_en;
	bool factor_m_en;
	uint32_t int_rate;

	if (rate < AIC_V1_PLL_DISP_FREQ_MIN_HZ) {
		factor_m = DIV_ROUND_UP(AIC_V1_PLL_DISP_FREQ_MIN_HZ, rate) - 1U;
	} else {
		factor_m = 0U;
	}

	if (factor_m == 2U) {
		factor_m = 3U;
	}

	if (factor_m > FIELD_GET(AIC_V1_PLL_FACTORM_MASK, UINT32_MAX)) {
		return -EINVAL;
	}

	factor_m_en = factor_m != 0U;
	vco_rate = (factor_m + 1U) * rate;
	if (vco_rate > AIC_V1_PLL_FRA2_VCO_MAX_HZ) {
		vco_rate = AIC_V1_PLL_FRA2_VCO_MAX_HZ;
	}

	factor_p = (vco_rate % AIC_V1_OSC24M_HZ) != 0U ? 1U : 0U;
	factor_n = (vco_rate * (factor_p + 1U)) / AIC_V1_OSC24M_HZ - 1U;

	K_SPINLOCK(&data->lock) {
		gen = sys_read32(config->base + AIC_V1_PLL_FRA2_GEN_REG);
		gen &= ~(AIC_V1_PLL_OUT_SYS_BIT | AIC_V1_PLL_EN_BIT);
		artinchip_v1_cmu_reg_write(config, AIC_V1_PLL_FRA2_GEN_REG, gen);
		k_busy_wait(50);

		artinchip_v1_clock_pll_fra2_bypass(config, true);

		artinchip_v1_cmu_reg_write(config, AIC_V1_PLL_FRA2_CFG_REG, 0U);

		gen = sys_read32(config->base + AIC_V1_PLL_FRA2_GEN_REG);
		gen &= ~(0xffffU | AIC_V1_PLL_FACTORM_EN_BIT);
		gen |= (factor_m_en ? AIC_V1_PLL_FACTORM_EN_BIT : 0U) |
		       FIELD_PREP(AIC_V1_PLL_FACTORN_MASK, factor_n) |
		       FIELD_PREP(AIC_V1_PLL_FACTORM_MASK, factor_m) |
		       FIELD_PREP(AIC_V1_PLL_FACTORP_MASK, factor_p);
		artinchip_v1_cmu_reg_write(config, AIC_V1_PLL_FRA2_GEN_REG, gen);

		int_rate = AIC_V1_OSC24M_HZ / (factor_p + 1U) * (factor_n + 1U) /
			   (factor_m + 1U);
		remainder = rate % int_rate;
		fra_en = remainder != 0U;
		if (fra_en) {
			fra_in = (uint64_t)remainder * (factor_p + 1U) * (factor_m + 1U) *
				 AIC_V1_PLL_FRAC_DIV_MASK;
			fra_in /= AIC_V1_OSC24M_HZ;
			cfg = AIC_V1_PLL_FRAC_EN_BIT | (uint32_t)fra_in;
		} else {
			cfg = 0U;
		}
		artinchip_v1_cmu_reg_write(config, AIC_V1_PLL_FRA2_CFG_REG, cfg);
		artinchip_v1_clock_pll_fra2_enable_sdm(config, factor_n);

		gen = sys_read32(config->base + AIC_V1_PLL_FRA2_GEN_REG);
		gen |= AIC_V1_PLL_OUT_SYS_BIT | AIC_V1_PLL_EN_BIT;
		artinchip_v1_cmu_reg_write(config, AIC_V1_PLL_FRA2_GEN_REG, gen);

		k_busy_wait(200);
		artinchip_v1_clock_pll_fra2_bypass(config, false);
	}

	return 0;
}

static int artinchip_v1_clock_enable_pll_fra2(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	if (!config->is_d12p) {
		return 0;
	}

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_V1_PLL_FRA2_GEN_REG);
		if (enable) {
			value |= AIC_V1_PLL_OUT_SYS_BIT | AIC_V1_PLL_EN_BIT;
		} else {
			value &= ~(AIC_V1_PLL_OUT_SYS_BIT | AIC_V1_PLL_EN_BIT);
		}
		artinchip_v1_cmu_reg_write(config, AIC_V1_PLL_FRA2_GEN_REG, value);
	}

	return 0;
}

static uint32_t artinchip_v1_clock_mipi_dsi_sclk_rate(const struct artinchip_clock_config *config)
{
	/*
	 * DSI serial clock: pixclk * bpp / lane_num (RGB888, 4 lanes).
	 */
	return (config->display_pixclk_rate * 24U) / 4U;
}

static int artinchip_v1_clock_configure_display_link(const struct device *dev, bool enable,
						     uint32_t sclk_target_hz)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t effective_sclk_rate;
	uint32_t pll_rate;
	uint32_t sclk_divider;
	uint32_t pix_divider;
	uint32_t sclk_target;
	uint32_t value;
	int ret;

	if (!config->is_d12p) {
		return 0;
	}

	if (sclk_target_hz == 0U) {
		sclk_target = config->display_sclk_rate;
	} else {
		sclk_target = sclk_target_hz;
	}

	if ((sclk_target == 0U) || (config->display_pixclk_rate == 0U) ||
	    (sclk_target < config->display_pixclk_rate)) {
		return -EINVAL;
	}

	if (!enable) {
		K_SPINLOCK(&data->lock) {
			value = sys_read32(config->base + AIC_V1_DISP_SCLK_REG);
			value &= ~AIC_V1_DISP_SCLK_GATE;
			sys_write32(value, config->base + AIC_V1_DISP_SCLK_REG);
		}
		return 0;
	}

	/*
	 * Program the display PLL for the target serial clock directly.
	 * set_pll_fra2_rate() already scales via FACTORM when rate < 360 MHz,
	 * matching luban-lite hal_clk_set_freq(CLK_PLL_FRA2, sclk_rate).
	 */
	for (int attempt = 0; attempt < 2; attempt++) {
		ret = artinchip_v1_clock_set_pll_fra2_rate(dev, sclk_target);
		if (ret != 0) {
			return ret;
		}

		pll_rate = artinchip_v1_clock_read_pll_fra2_rate(dev);
		if ((pll_rate != 0U) && artinchip_v1_clock_rate_close(pll_rate, sclk_target)) {
			break;
		}

		if (attempt == 1) {
			return -EIO;
		}
	}

	sclk_divider = DIV_ROUND_CLOSEST(pll_rate, sclk_target) - 1U;
	if (sclk_divider > AIC_V1_DISP_SCLK_DIV_MASK) {
		return -EINVAL;
	}

	effective_sclk_rate = pll_rate / (sclk_divider + 1U);
	if (effective_sclk_rate < config->display_pixclk_rate) {
		return -EINVAL;
	}

	pix_divider = DIV_ROUND_CLOSEST(effective_sclk_rate,
					config->display_pixclk_rate) - 1U;
	if (pix_divider > AIC_V1_DISP_PIXCLK_DIV_MASK) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_V1_DISP_SCLK_REG);
		value &= ~AIC_V1_DISP_SCLK_DIV_MASK;
		value |= FIELD_PREP(AIC_V1_DISP_SCLK_DIV_MASK, sclk_divider);
		value |= AIC_V1_DISP_SCLK_GATE;
		artinchip_v1_cmu_reg_write(config, AIC_V1_DISP_SCLK_REG, value);

		value = sys_read32(config->base + AIC_V1_DISP_PIXCLK_REG);
		value &= ~AIC_V1_DISP_PIXCLK_DIV_MASK;
		value |= FIELD_PREP(AIC_V1_DISP_PIXCLK_DIV_MASK, pix_divider);
		artinchip_v1_cmu_reg_write(config, AIC_V1_DISP_PIXCLK_REG, value);

		data->display_pll_rate = pll_rate;
		data->display_effective_sclk_rate = effective_sclk_rate;
		data->display_effective_pixclk_rate =
			effective_sclk_rate / (pix_divider + 1U);
	}

	ret = artinchip_v1_clock_enable_pll_fra2(dev, true);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

static int artinchip_v1_clock_gate_mipidsi(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t divider;
	uint32_t value;
	int ret;

	if (!config->is_d12p) {
		return artinchip_clock_gate_simple(dev, AIC_MIPIDSI_CLK_REG,
					      AIC_MIPIDSI_CLK_GATE_MASK, enable);
	}

	ret = artinchip_v1_clock_configure_display_link(dev, enable,
						      artinchip_v1_clock_mipi_dsi_sclk_rate(config));
	if (ret != 0) {
		return ret;
	}

	if (!enable) {
		return 0;
	}

	divider = DIV_ROUND_CLOSEST(data->display_pll_rate != 0U ? data->display_pll_rate :
				    artinchip_v1_clock_read_pll_fra2_rate(dev),
				    artinchip_v1_clock_mipi_dsi_sclk_rate(config)) - 1U;
	if (divider > AIC_V1_MIPIDSI_DIV_MASK) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		/*
		 * Luban-lite's D12x/ArtInChip v1 clock table does not expose a
		 * separate CLK_MIPIDSI; the DSI display interface shares the
		 * RGB clock gate at 0x0880. Keep programming 0x0888 as a
		 * compatibility/debug register, but make the RGB gate the
		 * effective ArtInChip v1 display-interface enable.
		 */
		value = sys_read32(config->base + AIC_RGB_CLK_REG);
		if (enable) {
			value |= artinchip_clock_display_gate_mask(config,
								   ARTINCHIP_CLK_MIPIDSI,
								   AIC_RGB_CLK_GATE_MASK);
		} else {
			value &= ~artinchip_clock_display_gate_mask(config,
								    ARTINCHIP_CLK_MIPIDSI,
								    AIC_RGB_CLK_GATE_MASK);
		}
		sys_write32(value, config->base + AIC_RGB_CLK_REG);

		value = sys_read32(config->base + AIC_MIPIDSI_CLK_REG);
		value &= ~AIC_V1_MIPIDSI_DIV_MASK;
		value |= FIELD_PREP(AIC_V1_MIPIDSI_DIV_MASK, divider);
		if (enable) {
			value |= AIC_V1_PRE_MOD_APB_GATE_MASK;
		} else {
			value &= ~AIC_V1_PRE_MOD_APB_GATE_MASK;
		}
		sys_write32(value, config->base + AIC_MIPIDSI_CLK_REG);
	}

	return 0;
}

static int artinchip_clock_gate_pwmcs(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t divider = DIV_ROUND_CLOSEST(config->parent_rate, AIC_PWMCS_CLK_DEFAULT_HZ) - 1U;
	uint32_t value;

	if (divider > AIC_PWMCS_CLK_DIV_MASK) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_PWMCS_CLK_REG);
		value &= ~AIC_PWMCS_CLK_DIV_MASK;
		value |= FIELD_PREP(AIC_PWMCS_CLK_DIV_MASK, divider);
		value = enable ? (value | AIC_SIMPLE_CLK_BUS_GATE | AIC_PWMCS_CLK_MOD_GATE) :
				 (value & ~(AIC_SIMPLE_CLK_BUS_GATE | AIC_PWMCS_CLK_MOD_GATE));
		sys_write32(value, config->base + AIC_PWMCS_CLK_REG);
	}
	return 0;
}

static int artinchip_clock_gate_psadc(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t divider;
	uint32_t value;

	divider = DIV_ROUND_CLOSEST(config->parent_rate, AIC_PSADC_CLK_DEFAULT_HZ) - 1U;
	if (divider > AIC_PSADC_CLK_DIV_MASK) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_PSADC_CLK_REG);
		value &= ~AIC_PSADC_CLK_DIV_MASK;
		value |= FIELD_PREP(AIC_PSADC_CLK_DIV_MASK, divider);
		if (enable) {
			value |= AIC_PSADC_CLK_BUS_GATE | AIC_PSADC_CLK_MOD_GATE;
		} else {
			value &= ~(AIC_PSADC_CLK_BUS_GATE | AIC_PSADC_CLK_MOD_GATE);
		}
		sys_write32(value, config->base + AIC_PSADC_CLK_REG);
	}

	return 0;
}

static uint32_t artinchip_clock_sdmc_reg_offset(const struct artinchip_clock_config *config,
						uint32_t id)
{
	uint32_t base = config->is_d12p ? AIC_V1_SDMC_CLK_REG_BASE : AIC_SDMC_CLK_REG_BASE;

	return base + ((id - ARTINCHIP_CLK_SDMC0) * 4U);
}

static uint32_t artinchip_clock_sdmc_gate_mask(const struct artinchip_clock_config *config)
{
	return config->is_d12p ? AIC_V1_SDMC_CLK_GATE_MASK
			       : (AIC_SDMC_CLK_BUS_GATE | AIC_SDMC_CLK_MOD_GATE);
}

static uint32_t artinchip_clock_sdmc_div_mask(const struct artinchip_clock_config *config)
{
	return config->is_d12p ? AIC_V1_SDMC_CLK_DIV_MASK : AIC_SDMC_CLK_DIV_MASK;
}

static int artinchip_clock_gate_sdmc(const struct device *dev, uint32_t id, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t offset;
	uint32_t gate_mask;
	uint32_t div_mask;
	uint32_t divider;
	uint32_t value;

	if (!artinchip_clock_is_sdmc(id)) {
		return -EINVAL;
	}

	offset = artinchip_clock_sdmc_reg_offset(config, id);
	gate_mask = artinchip_clock_sdmc_gate_mask(config);
	div_mask = artinchip_clock_sdmc_div_mask(config);
	divider = DIV_ROUND_CLOSEST(AIC_SDMC_CLK_PARENT_HZ, AIC_SDMC_CLK_DEFAULT_HZ);
	if ((divider == 0U) || (divider > (div_mask + 1U))) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + offset);
		value &= ~div_mask;
		value |= FIELD_PREP(div_mask, divider - 1U);
		if (enable) {
			value |= gate_mask;
		} else {
			value &= ~gate_mask;
		}
		sys_write32(value, config->base + offset);
	}

	return 0;
}

static int artinchip_clock_gate_spienc(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_SPIENC_CLK_REG);
		if (enable) {
			value |= AIC_SPIENC_CLK_BUS_GATE | AIC_SPIENC_CLK_MOD_GATE;
		} else {
			value &= ~(AIC_SPIENC_CLK_BUS_GATE | AIC_SPIENC_CLK_MOD_GATE);
		}
		sys_write32(value, config->base + AIC_SPIENC_CLK_REG);
	}

	return 0;
}

static int artinchip_clock_gate_wdt(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_WDT_CLK_REG);
		if (enable) {
			value |= AIC_WDT_CLK_BUS_GATE | AIC_WDT_CLK_MOD_GATE;
		} else {
			value &= ~(AIC_WDT_CLK_BUS_GATE | AIC_WDT_CLK_MOD_GATE);
		}
		sys_write32(value, config->base + AIC_WDT_CLK_REG);
	}

	return 0;
}

static int artinchip_clock_gate_sid(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_SID_CLK_REG);
		if (enable) {
			value |= AIC_SID_CLK_BUS_GATE | AIC_SID_CLK_MOD_GATE;
		} else {
			value &= ~(AIC_SID_CLK_BUS_GATE | AIC_SID_CLK_MOD_GATE);
		}
		sys_write32(value, config->base + AIC_SID_CLK_REG);
	}

	return 0;
}

static uint32_t artinchip_clock_i2c_reg_offset(uint32_t id)
{
	return AIC_I2C_CLK_REG_BASE + ((id - ARTINCHIP_CLK_I2C0) * 4U);
}

static uint32_t artinchip_clock_i2c_gate_mask(const struct artinchip_clock_config *config)
{
	return config->is_d12p ? AIC_V1_I2C_CLK_BUS_GATE : AIC_I2C_CLK_BUS_GATE;
}

static int artinchip_clock_gate_i2c(const struct device *dev, uint32_t id, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t gate_mask = artinchip_clock_i2c_gate_mask(config);
	uint32_t value;

	if (!artinchip_clock_is_i2c(id)) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + artinchip_clock_i2c_reg_offset(id));
		if (enable) {
			value |= gate_mask;
		} else {
			value &= ~gate_mask;
		}
		sys_write32(value, config->base + artinchip_clock_i2c_reg_offset(id));
	}

	return 0;
}

static int artinchip_clock_gate_pwm(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t divider;
	uint32_t value;

	divider = DIV_ROUND_CLOSEST(config->parent_rate, config->pwm_rate);
	if ((divider == 0U) || (divider > (AIC_PWM_CLK_DIV_MASK + 1U))) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_PWM_CLK_REG);
		value &= ~AIC_PWM_CLK_DIV_MASK;
		value |= FIELD_PREP(AIC_PWM_CLK_DIV_MASK, divider - 1U);
		if (enable) {
			value |= AIC_PWM_CLK_BUS_GATE | AIC_PWM_CLK_MOD_GATE;
		} else {
			value &= ~(AIC_PWM_CLK_BUS_GATE | AIC_PWM_CLK_MOD_GATE);
		}
		sys_write32(value, config->base + AIC_PWM_CLK_REG);
	}

	return 0;
}

static int artinchip_clock_gate_xspi(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_XSPI_CLK_REG);
		if (enable) {
			value |= AIC_XSPI_CLK_BUS_GATE | AIC_XSPI_CLK_MOD_GATE;
		} else {
			value &= ~(AIC_XSPI_CLK_BUS_GATE | AIC_XSPI_CLK_MOD_GATE);
		}
		sys_write32(value, config->base + AIC_XSPI_CLK_REG);
	}

	return 0;
}

static int artinchip_clock_gate_syscfg(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_SYSCFG_CLK_REG);
		if (enable) {
			value |= AIC_SYSCFG_CLK_BUS_GATE;
		} else {
			value &= ~AIC_SYSCFG_CLK_BUS_GATE;
		}
		sys_write32(value, config->base + AIC_SYSCFG_CLK_REG);
	}

	return 0;
}

static int artinchip_clock_gate_rtc(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_RTC_CLK_REG);
		if (enable) {
			value |= AIC_RTC_CLK_BUS_GATE;
		} else {
			value &= ~AIC_RTC_CLK_BUS_GATE;
		}
		sys_write32(value, config->base + AIC_RTC_CLK_REG);
	}

	return 0;
}

static int artinchip_clock_gate_tsen(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_TSEN_CLK_REG);
		if (enable) {
			value |= AIC_TSEN_CLK_BUS_GATE;
		} else {
			value &= ~AIC_TSEN_CLK_BUS_GATE;
		}
		sys_write32(value, config->base + AIC_TSEN_CLK_REG);
	}

	return 0;
}

static int artinchip_clock_gate_rtp(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_RTP_CLK_REG);
		if (enable) {
			value |= AIC_RTP_CLK_BUS_GATE;
		} else {
			value &= ~AIC_RTP_CLK_BUS_GATE;
		}
		sys_write32(value, config->base + AIC_RTP_CLK_REG);
	}

	return 0;
}

/* D12P protected module registers require one authorization per write. */
static int artinchip_d12p_module_write(const struct artinchip_clock_config *config,
				      uint32_t offset, uint32_t value)
{
	sys_write32((AIC_V1_CMU_AUTH_KEY << 20) | offset,
		    config->base + AIC_V1_CMU_WR_CFG_REG);
	for (unsigned int i = 0U; i < 100U; i++) {
		if (sys_read32(config->base + AIC_V1_CMU_WR_CFG_REG) & AIC_V1_CMU_AUTH_OK_BIT) {
			sys_write32(value, config->base + offset);
			return sys_read32(config->base + offset) == value ? 0 : -EIO;
		}
		k_busy_wait(10);
	}
	return -ETIMEDOUT;
}

static uint32_t artinchip_clock_uart_parent_rate(const struct artinchip_clock_config *config)
{
	uint32_t pll;

	pll = sys_read32(config->base + 0x4U); /* PLL_INT1 */
	if (!(pll & BIT(20))) {
		return AIC_V1_OSC24M_HZ;
	}
	return AIC_V1_OSC24M_HZ / ((pll & 1U) + 1U) * (((pll >> 8) & 0xffU) + 1U) /
	       (((pll >> 4) & 3U) + 1U);
}

static int artinchip_clock_gate_gpio(const struct device *dev, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t value;
	uint32_t mask = config->is_d12p ? BIT(21) : AIC_GPIO_CLK_BUS_GATE;
	int ret = 0;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_GPIO_CLK_REG);
		if (enable) {
			value |= mask;
		} else {
			value &= ~mask;
		}
		if (config->is_d12p) {
			ret = artinchip_d12p_module_write(config, AIC_GPIO_CLK_REG, value);
		} else {
			sys_write32(value, config->base + AIC_GPIO_CLK_REG);
		}
	}

	return ret;
}

static uint32_t artinchip_clock_qspi_reg_offset(uint32_t id)
{
	return AIC_QSPI_CLK_REG_BASE + ((id - ARTINCHIP_CLK_QSPI0) * 4U);
}

static int artinchip_clock_gate_qspi(const struct device *dev, uint32_t id, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t offset;
	uint32_t value;
	int ret = 0;

	if (!artinchip_clock_is_qspi(id)) {
		return -EINVAL;
	}

	offset = artinchip_clock_qspi_reg_offset(id);
	if (config->is_d21x) {
		uint32_t divider;

		if (config->qspi_rate == 0U) {
			return -EINVAL;
		}
		divider = DIV_ROUND_UP(artinchip_d12p_qspi_parent_rate(dev), config->qspi_rate);

		if (divider == 0U || divider > 32U) {
			return -EINVAL;
		}
		K_SPINLOCK(&data->lock) {
			value = sys_read32(config->base + offset);
			value &= ~(GENMASK(4, 0) | BIT(12) | BIT(8));
			value |= divider - 1U;
			if (enable) {
				value |= BIT(12) | BIT(8);
			}
			sys_write32(value, config->base + offset);
		}
		return 0;
	}
	if (config->is_d12p) {
		uint32_t divider;
		uint32_t gates[] = {BIT(17), BIT(16), BIT(21), BIT(22)};
		uint32_t mask = BIT(17) | BIT(16) | BIT(21) | BIT(22);

		if (config->qspi_rate == 0U) {
			return -EINVAL;
		}
		divider = DIV_ROUND_UP(artinchip_d12p_qspi_parent_rate(dev), config->qspi_rate);
		if (divider == 0U || divider > 16U) {
			return -EINVAL;
		}
		K_SPINLOCK(&data->lock) {
			value = sys_read32(config->base + offset);
			value = (value & ~(mask | GENMASK(3, 0))) | (divider - 1U);
			ret = artinchip_d12p_module_write(config, offset, value);
			for (size_t i = 0; enable && ret == 0 && i < ARRAY_SIZE(gates); i++) {
				value |= gates[i];
				ret = artinchip_d12p_module_write(config, offset, value);
			}
		}
		return ret;
	}

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + offset);
		if (enable) {
			value |= AIC_QSPI_CLK_BUS_GATE | AIC_QSPI_CLK_MOD_GATE;
		} else {
			value &= ~(AIC_QSPI_CLK_BUS_GATE | AIC_QSPI_CLK_MOD_GATE);
		}
		sys_write32(value, config->base + offset);
	}

	return 0;
}

static int artinchip_clock_uart_index(uint32_t id, uint32_t *index)
{
	if (!artinchip_clock_is_uart(id)) {
		return -EINVAL;
	}

	*index = id - ARTINCHIP_CLK_UART0;

	return 0;
}

static uint32_t artinchip_clock_uart_reg_offset(uint32_t index)
{
	return AIC_UART_CLK_REG_BASE + (index * 4U);
}

static uint32_t artinchip_clock_uart_target_rate(const struct device *dev, uint32_t index)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;

	if (data->uart_rates[index] != 0U) {
		return data->uart_rates[index];
	}

	if (index == 0U && config->default_uart_rate != 0U) {
		return config->default_uart_rate;
	}

	return AIC_UART_CLK_DEFAULT_HZ;
}

static int artinchip_clock_apply_uart_rate(const struct device *dev, uint32_t index, bool enable)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t target_rate = artinchip_clock_uart_target_rate(dev, index);
	uint32_t offset = artinchip_clock_uart_reg_offset(index);
	uint32_t gate_mask = config->is_d12p ? AIC_UART_CLK_GATE_MASK :
		AIC_D13X_UART_CLK_GATE_MASK;
	uint32_t div_mask = config->is_d12p ? AIC_UART_CLK_DIV_MASK :
		AIC_D13X_UART_CLK_DIV_MASK;
	uint32_t divider;
	uint32_t value;
	uint32_t parent = artinchip_clock_uart_parent_rate(config);
	int ret = 0;

	if ((parent == 0U) || (target_rate == 0U) ||
	    (target_rate > parent)) {
		return -EINVAL;
	}

	divider = DIV_ROUND_CLOSEST(parent, target_rate);
	if (divider > (div_mask + 1U)) {
		return -EINVAL;
	}

	divider--;

	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + offset);
		value &= ~div_mask;
		value |= divider;

		if (enable) {
			value |= gate_mask;
		} else {
			value &= ~gate_mask;
		}

		if (config->is_d12p) {
			uint32_t gates[] = {BIT(17), BIT(16), BIT(20)};

			value &= ~gate_mask;
			ret = artinchip_d12p_module_write(config, offset, value);
			for (size_t i = 0; enable && ret == 0 && i < ARRAY_SIZE(gates); i++) {
				value |= gates[i];
				ret = artinchip_d12p_module_write(config, offset, value);
			}
		} else {
			sys_write32(value, config->base + offset);
		}
	}

	return ret;
}

static int artinchip_d13x_clock_enable_lvds(const struct device *dev)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t rate = config->display_sclk_rate;
	uint32_t value;
	int ret;

	/* Single-link LVDS serializes seven bits per pixel clock. */
	if (config->is_d12p || config->display_pixclk_rate == 0U ||
	    (uint64_t)config->display_pixclk_rate * 7U != rate) {
		return -ENOTSUP;
	}

	ret = artinchip_v1_clock_set_pll_fra2_rate(dev, rate);
	if (ret != 0) {
		return ret;
	}

	K_SPINLOCK(&data->lock) {
		/* D13x packs SCLK and pixel dividers into the same register. */
		value = sys_read32(config->base + AIC_V1_DISP_SCLK_REG);
		value &= ~AIC_D13X_DISP_DIV_MASK;
		value |= FIELD_PREP(AIC_D13X_DISP_PIX_DIV_MASK, 6U);
		sys_write32(value, config->base + AIC_V1_DISP_SCLK_REG);
		data->display_pll_rate = artinchip_v1_clock_read_pll_fra2_rate(dev);
		data->display_effective_sclk_rate = data->display_pll_rate;
		data->display_effective_pixclk_rate = data->display_pll_rate / 7U;
	}

	return artinchip_clock_gate_simple(dev, AIC_LVDS_CLK_REG,
					 AIC_LVDS_CLK_GATE_MASK, true);
}

static int artinchip_d13x_clock_enable_de(const struct device *dev)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t parent = artinchip_clock_uart_parent_rate(config);
	uint32_t divider = DIV_ROUND_UP(parent, AIC_D13X_DE_MAX_HZ);
	uint32_t value;

	if (divider == 0U || divider > AIC_D13X_DE_DIV_MASK + 1U) {
		return -EINVAL;
	}
	K_SPINLOCK(&data->lock) {
		value = sys_read32(config->base + AIC_DE_CLK_REG);
		value &= ~AIC_D13X_DE_DIV_MASK;
		value |= (divider - 1U) | AIC_DE_CLK_GATE_MASK;
		sys_write32(value, config->base + AIC_DE_CLK_REG);
	}
	return 0;
}

static int artinchip_clock_on(const struct device *dev, clock_control_subsys_t sys)
{
	uint32_t id = POINTER_TO_UINT(sys);
	const struct artinchip_clock_config *config = dev->config;
	uint32_t index;
	int ret;

	if (artinchip_clock_is_qspi(id)) {
		return artinchip_clock_gate_qspi(dev, id, true);
	}

	if (id == ARTINCHIP_CLK_GPIO) {
		return artinchip_clock_gate_gpio(dev, true);
	}

	if (artinchip_clock_is_i2c(id)) {
		return artinchip_clock_gate_i2c(dev, id, true);
	}

	if (id == ARTINCHIP_CLK_PWM) {
		return artinchip_clock_gate_pwm(dev, true);
	}

	if (id == ARTINCHIP_CLK_XSPI) {
		return artinchip_clock_gate_xspi(dev, true);
	}

	if (id == ARTINCHIP_CLK_SYSCFG) {
		return artinchip_clock_gate_syscfg(dev, true);
	}

	if (id == ARTINCHIP_CLK_RTC) {
		return artinchip_clock_gate_rtc(dev, true);
	}

	if (id == ARTINCHIP_CLK_TSEN) {
		return artinchip_clock_gate_tsen(dev, true);
	}

	if (id == ARTINCHIP_CLK_RTP) {
		return artinchip_clock_gate_rtp(dev, true);
	}

	if (artinchip_clock_is_sdmc(id)) {
		return artinchip_clock_gate_sdmc(dev, id, true);
	}

	if (id == ARTINCHIP_CLK_SPIENC) {
		return artinchip_clock_gate_spienc(dev, true);
	}

	if (id == ARTINCHIP_CLK_WDT) {
		return artinchip_clock_gate_wdt(dev, true);
	}

	if (id == ARTINCHIP_CLK_SID) {
		return artinchip_clock_gate_sid(dev, true);
	}

	if (artinchip_clock_is_can(id)) {
		return artinchip_clock_gate_can(dev, id, true);
	}

	if (id == ARTINCHIP_CLK_ADCIM) {
		return artinchip_clock_gate_adcim(dev, true);
	}

	if (id == ARTINCHIP_CLK_GPAI) {
		return artinchip_clock_gate_gpai(dev, true);
	}

	if (id == ARTINCHIP_CLK_PSADC) {
		return artinchip_clock_gate_psadc(dev, true);
	}

	if (id == ARTINCHIP_CLK_CIR) {
		return artinchip_clock_gate_cir(dev, true);
	}

	if (id == ARTINCHIP_CLK_MTOP) {
		return artinchip_clock_gate_simple(dev, AIC_MTOP_CLK_REG, AIC_SIMPLE_CLK_BUS_GATE, true);
	}
	if (id == ARTINCHIP_CLK_PBUS) {
		return artinchip_clock_gate_simple(dev, AIC_PBUS_CLK_REG, AIC_SIMPLE_CLK_BUS_GATE, true);
	}
	if (id == ARTINCHIP_CLK_PWMCS) {
		return artinchip_clock_gate_pwmcs(dev, true);
	}
	if (id == ARTINCHIP_CLK_I2S0) {
		return artinchip_clock_gate_simple(dev, AIC_I2S0_CLK_REG, AIC_I2S0_CLK_GATE_MASK, true);
	}
	if (id == ARTINCHIP_CLK_CODEC) {
		return artinchip_clock_gate_simple(dev, AIC_CODEC_CLK_REG, AIC_CODEC_CLK_GATE_MASK, true);
	}
	if (id == ARTINCHIP_CLK_GMAC0) {
		return artinchip_clock_gate_simple(dev, AIC_GMAC0_CLK_REG, AIC_GMAC0_CLK_GATE_MASK, true);
	}
	if (id == ARTINCHIP_CLK_CE) {
		return artinchip_clock_gate_simple(dev, AIC_CE_CLK_REG, AIC_CE_CLK_GATE_MASK, true);
	}
	if (id == ARTINCHIP_CLK_GE) {
		return artinchip_clock_gate_simple(
			dev, AIC_GE_CLK_REG,
			artinchip_clock_display_gate_mask(config, id, AIC_GE_CLK_GATE_MASK), true);
	}
	if (id == ARTINCHIP_CLK_VE) {
		return artinchip_clock_gate_simple(dev, AIC_VE_CLK_REG, AIC_VE_CLK_GATE_MASK, true);
	}
	if (id == ARTINCHIP_CLK_MIPIDSI) {
		return artinchip_v1_clock_gate_mipidsi(dev, true);
	}
	if (id == ARTINCHIP_CLK_LVDS) {
		return artinchip_d13x_clock_enable_lvds(dev);
	}
	if (id == ARTINCHIP_CLK_RGB) {
		ret = artinchip_v1_clock_configure_display_link(dev, true, 0);
		if (ret != 0) {
			return ret;
		}
		return artinchip_clock_gate_simple(
			dev, AIC_RGB_CLK_REG,
			artinchip_clock_display_gate_mask(config, id, AIC_RGB_CLK_GATE_MASK), true);
	}
	if (id == ARTINCHIP_CLK_DE) {
		if (!config->is_d12p) {
			return artinchip_d13x_clock_enable_de(dev);
		}
		ret = artinchip_v1_clock_configure_display_link(dev, true, 0);
		if (ret != 0) {
			return ret;
		}
		return artinchip_clock_gate_simple(
			dev, AIC_DE_CLK_REG,
			artinchip_clock_display_gate_mask(config, id, AIC_DE_CLK_GATE_MASK), true);
	}
	if (id == ARTINCHIP_CLK_DVP) {
		return artinchip_clock_gate_simple(
			dev, AIC_DVP_CLK_REG,
			artinchip_clock_display_gate_mask(config, id, AIC_DVP_CLK_GATE_MASK), true);
	}
	if (id == ARTINCHIP_CLK_USBD) {
		return artinchip_clock_gate_simple(
			dev, AIC_USBD_CLK_REG,
			artinchip_clock_usb_gate_mask(config, id, AIC_USBD_CLK_GATE_MASK), true);
	}
	if (id == ARTINCHIP_CLK_USBH0) {
		return artinchip_clock_gate_simple(
			dev, AIC_USBH0_CLK_REG,
			artinchip_clock_usb_gate_mask(config, id, AIC_USBH0_CLK_GATE_MASK), true);
	}
	if (id == ARTINCHIP_CLK_USB_PHY0) {
		return artinchip_clock_gate_simple(
			dev, AIC_USB_PHY0_CLK_REG,
			artinchip_clock_usb_gate_mask(config, id, AIC_USB_PHY0_CLK_GATE_MASK),
			true);
	}

	ret = artinchip_clock_uart_index(id, &index);
	if (ret != 0) {
		return ret;
	}

	return artinchip_clock_apply_uart_rate(dev, index, true);
}

static int artinchip_clock_off(const struct device *dev, clock_control_subsys_t sys)
{
	uint32_t id = POINTER_TO_UINT(sys);
	const struct artinchip_clock_config *config = dev->config;
	uint32_t index;
	int ret;

	if (artinchip_clock_is_qspi(id)) {
		return artinchip_clock_gate_qspi(dev, id, false);
	}

	if (id == ARTINCHIP_CLK_GPIO) {
		return artinchip_clock_gate_gpio(dev, false);
	}

	if (artinchip_clock_is_i2c(id)) {
		return artinchip_clock_gate_i2c(dev, id, false);
	}

	if (id == ARTINCHIP_CLK_PWM) {
		return artinchip_clock_gate_pwm(dev, false);
	}

	if (id == ARTINCHIP_CLK_XSPI) {
		return artinchip_clock_gate_xspi(dev, false);
	}

	if (id == ARTINCHIP_CLK_SYSCFG) {
		return artinchip_clock_gate_syscfg(dev, false);
	}

	if (id == ARTINCHIP_CLK_RTC) {
		return artinchip_clock_gate_rtc(dev, false);
	}

	if (id == ARTINCHIP_CLK_TSEN) {
		return artinchip_clock_gate_tsen(dev, false);
	}

	if (id == ARTINCHIP_CLK_RTP) {
		return artinchip_clock_gate_rtp(dev, false);
	}

	if (artinchip_clock_is_sdmc(id)) {
		return artinchip_clock_gate_sdmc(dev, id, false);
	}

	if (id == ARTINCHIP_CLK_SPIENC) {
		return artinchip_clock_gate_spienc(dev, false);
	}

	if (id == ARTINCHIP_CLK_WDT) {
		return artinchip_clock_gate_wdt(dev, false);
	}

	if (id == ARTINCHIP_CLK_SID) {
		return artinchip_clock_gate_sid(dev, false);
	}

	if (artinchip_clock_is_can(id)) {
		return artinchip_clock_gate_can(dev, id, false);
	}

	if (id == ARTINCHIP_CLK_ADCIM) {
		return artinchip_clock_gate_adcim(dev, false);
	}

	if (id == ARTINCHIP_CLK_GPAI) {
		return artinchip_clock_gate_gpai(dev, false);
	}

	if (id == ARTINCHIP_CLK_PSADC) {
		return artinchip_clock_gate_psadc(dev, false);
	}

	if (id == ARTINCHIP_CLK_CIR) {
		return artinchip_clock_gate_cir(dev, false);
	}

	if (id == ARTINCHIP_CLK_MTOP) {
		return artinchip_clock_gate_simple(dev, AIC_MTOP_CLK_REG, AIC_SIMPLE_CLK_BUS_GATE, false);
	}
	if (id == ARTINCHIP_CLK_PBUS) {
		return artinchip_clock_gate_simple(dev, AIC_PBUS_CLK_REG, AIC_SIMPLE_CLK_BUS_GATE, false);
	}
	if (id == ARTINCHIP_CLK_PWMCS) {
		return artinchip_clock_gate_pwmcs(dev, false);
	}
	if (id == ARTINCHIP_CLK_I2S0) {
		return artinchip_clock_gate_simple(dev, AIC_I2S0_CLK_REG, AIC_I2S0_CLK_GATE_MASK, false);
	}
	if (id == ARTINCHIP_CLK_CODEC) {
		return artinchip_clock_gate_simple(dev, AIC_CODEC_CLK_REG, AIC_CODEC_CLK_GATE_MASK, false);
	}
	if (id == ARTINCHIP_CLK_GMAC0) {
		return artinchip_clock_gate_simple(dev, AIC_GMAC0_CLK_REG, AIC_GMAC0_CLK_GATE_MASK, false);
	}
	if (id == ARTINCHIP_CLK_CE) {
		return artinchip_clock_gate_simple(dev, AIC_CE_CLK_REG, AIC_CE_CLK_GATE_MASK, false);
	}
	if (id == ARTINCHIP_CLK_GE) {
		return artinchip_clock_gate_simple(
			dev, AIC_GE_CLK_REG,
			artinchip_clock_display_gate_mask(config, id, AIC_GE_CLK_GATE_MASK), false);
	}
	if (id == ARTINCHIP_CLK_VE) {
		return artinchip_clock_gate_simple(dev, AIC_VE_CLK_REG, AIC_VE_CLK_GATE_MASK, false);
	}
	if (id == ARTINCHIP_CLK_MIPIDSI) {
		return artinchip_v1_clock_gate_mipidsi(dev, false);
	}
	if (id == ARTINCHIP_CLK_LVDS) {
		return artinchip_clock_gate_simple(dev, AIC_LVDS_CLK_REG,
						 AIC_LVDS_CLK_GATE_MASK, false);
	}
	if (id == ARTINCHIP_CLK_RGB) {
		return artinchip_clock_gate_simple(
			dev, AIC_RGB_CLK_REG,
			artinchip_clock_display_gate_mask(config, id, AIC_RGB_CLK_GATE_MASK), false);
	}
	if (id == ARTINCHIP_CLK_DE) {
		return artinchip_clock_gate_simple(
			dev, AIC_DE_CLK_REG,
			artinchip_clock_display_gate_mask(config, id, AIC_DE_CLK_GATE_MASK), false);
	}
	if (id == ARTINCHIP_CLK_DVP) {
		return artinchip_clock_gate_simple(
			dev, AIC_DVP_CLK_REG,
			artinchip_clock_display_gate_mask(config, id, AIC_DVP_CLK_GATE_MASK), false);
	}
	if (id == ARTINCHIP_CLK_USBD) {
		return artinchip_clock_gate_simple(
			dev, AIC_USBD_CLK_REG,
			artinchip_clock_usb_gate_mask(config, id, AIC_USBD_CLK_GATE_MASK), false);
	}
	if (id == ARTINCHIP_CLK_USBH0) {
		return artinchip_clock_gate_simple(
			dev, AIC_USBH0_CLK_REG,
			artinchip_clock_usb_gate_mask(config, id, AIC_USBH0_CLK_GATE_MASK), false);
	}
	if (id == ARTINCHIP_CLK_USB_PHY0) {
		return artinchip_clock_gate_simple(
			dev, AIC_USB_PHY0_CLK_REG,
			artinchip_clock_usb_gate_mask(config, id, AIC_USB_PHY0_CLK_GATE_MASK),
			false);
	}

	ret = artinchip_clock_uart_index(id, &index);
	if (ret != 0) {
		return ret;
	}

	return artinchip_clock_apply_uart_rate(dev, index, false);
}

static int artinchip_clock_get_rate(const struct device *dev, clock_control_subsys_t sys,
			       uint32_t *rate)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t id = POINTER_TO_UINT(sys);
	uint32_t index;
	uint32_t offset;
	uint32_t divider;
	int ret;

	if (artinchip_clock_is_qspi(id)) {
		if (config->is_d12p) {
			uint32_t div = sys_read32(config->base +
						  artinchip_clock_qspi_reg_offset(id));

			*rate = artinchip_d12p_qspi_parent_rate(dev) / ((div & 0xfU) + 1U);
		} else {
			if (config->is_d21x) {
				uint32_t div = sys_read32(config->base +
							 artinchip_clock_qspi_reg_offset(id));

				*rate = artinchip_d12p_qspi_parent_rate(dev) /
					((div & GENMASK(4, 0)) + 1U);
			} else {
				*rate = config->qspi_rate;
			}
		}
		return 0;
	}

	if (id == ARTINCHIP_CLK_GPIO) {
		return -ENOTSUP;
	}

	if (artinchip_clock_is_i2c(id)) {
		*rate = config->is_d12p ? config->apb0_rate : config->apb1_rate;
		return 0;
	}

	if (id == ARTINCHIP_CLK_PWM) {
		*rate = config->pwm_rate;
		return 0;
	}

	if (id == ARTINCHIP_CLK_XSPI) {
		*rate = config->xspi_rate;
		return 0;
	}

	if (id == ARTINCHIP_CLK_SYSCFG) {
		*rate = 24000000U;
		return 0;
	}

	if (id == ARTINCHIP_CLK_RTC) {
		*rate = 32768U;
		return 0;
	}

	if (id == ARTINCHIP_CLK_TSEN) {
		*rate = config->apb1_rate;
		return 0;
	}

	if (id == ARTINCHIP_CLK_RTP) {
		*rate = config->apb1_rate;
		return 0;
	}

	if (artinchip_clock_is_sdmc(id)) {
		uint32_t value = sys_read32(config->base +
					    artinchip_clock_sdmc_reg_offset(config, id));

		divider = FIELD_GET(artinchip_clock_sdmc_div_mask(config), value) + 1U;
		*rate = AIC_SDMC_CLK_PARENT_HZ / divider;
		return 0;
	}

	if (id == ARTINCHIP_CLK_SPIENC) {
		*rate = config->parent_rate;
		return 0;
	}

	if (id == ARTINCHIP_CLK_WDT) {
		*rate = 32000U;
		return 0;
	}

	if (id == ARTINCHIP_CLK_SID) {
		*rate = 24000000U;
		return 0;
	}

	if (artinchip_clock_is_can(id)) {
		*rate = config->apb1_rate;
		return 0;
	}

	if (id == ARTINCHIP_CLK_ADCIM) {
		*rate = AIC_ADCIM_CLK_DEFAULT_HZ;
		return 0;
	}

	if (id == ARTINCHIP_CLK_GPAI) {
		*rate = config->apb1_rate;
		return 0;
	}

	if (id == ARTINCHIP_CLK_PSADC) {
		*rate = AIC_PSADC_CLK_DEFAULT_HZ;
		return 0;
	}

	if (id == ARTINCHIP_CLK_CIR) {
		*rate = config->apb1_rate;
		return 0;
	}

	if (id == ARTINCHIP_CLK_PWMCS) {
		*rate = AIC_PWMCS_CLK_DEFAULT_HZ;
		return 0;
	}
	if ((id == ARTINCHIP_CLK_MTOP) || (id == ARTINCHIP_CLK_PBUS)) {
		return -ENOTSUP;
	}
	if (id == ARTINCHIP_CLK_I2S0) {
		*rate = AIC_I2S0_CLK_DEFAULT_HZ;
		return 0;
	}
	if (id == ARTINCHIP_CLK_CODEC) {
		*rate = AIC_CODEC_CLK_DEFAULT_HZ;
		return 0;
	}
	if (id == ARTINCHIP_CLK_GMAC0) {
		*rate = AIC_GMAC0_CLK_DEFAULT_HZ;
		return 0;
	}
	if (id == ARTINCHIP_CLK_CE) {
		*rate = AIC_CE_CLK_DEFAULT_HZ;
		return 0;
	}
	if (id == ARTINCHIP_CLK_GE) {
		*rate = AIC_GE_CLK_DEFAULT_HZ;
		return 0;
	}
	if (id == ARTINCHIP_CLK_VE) {
		*rate = AIC_VE_CLK_DEFAULT_HZ;
		return 0;
	}
	if (id == ARTINCHIP_CLK_MIPIDSI) {
		*rate = config->is_d12p ? data->display_effective_sclk_rate :
			AIC_MIPIDSI_CLK_DEFAULT_HZ;
		return 0;
	}
	if (id == ARTINCHIP_CLK_LVDS) {
		*rate = data->display_effective_sclk_rate;
		return 0;
	}
	if (id == ARTINCHIP_CLK_RGB) {
		*rate = config->is_d12p ? data->display_effective_pixclk_rate :
			AIC_MIPIDSI_CLK_DEFAULT_HZ;
		return 0;
	}
	if (id == ARTINCHIP_CLK_DE) {
		if (!config->is_d12p) {
			divider = (sys_read32(config->base + AIC_DE_CLK_REG) &
				   AIC_D13X_DE_DIV_MASK) + 1U;
			*rate = artinchip_clock_uart_parent_rate(config) / divider;
			return 0;
		}
		*rate = AIC_DE_CLK_DEFAULT_HZ;
		return 0;
	}
	if (id == ARTINCHIP_CLK_DVP) {
		*rate = AIC_DVP_CLK_DEFAULT_HZ;
		return 0;
	}
	if ((id == ARTINCHIP_CLK_USBD) || (id == ARTINCHIP_CLK_USBH0)) {
		*rate = AIC_USB_BUS_CLK_DEFAULT_HZ;
		return 0;
	}
	if (id == ARTINCHIP_CLK_USB_PHY0) {
		*rate = AIC_USB_PHY_CLK_DEFAULT_HZ;
		return 0;
	}

	ret = artinchip_clock_uart_index(id, &index);
	if (ret != 0) {
		return ret;
	}

	offset = artinchip_clock_uart_reg_offset(index);
	divider = (sys_read32(config->base + offset) &
		   (config->is_d12p ? AIC_UART_CLK_DIV_MASK : AIC_D13X_UART_CLK_DIV_MASK)) + 1U;
	*rate = artinchip_clock_uart_parent_rate(config) / divider;

	return 0;
}

static enum clock_control_status artinchip_clock_get_status(const struct device *dev,
						       clock_control_subsys_t sys)
{
	const struct artinchip_clock_config *config = dev->config;
	uint32_t id = POINTER_TO_UINT(sys);
	uint32_t uart_gate_mask = config->is_d12p ? AIC_UART_CLK_GATE_MASK :
		AIC_D13X_UART_CLK_GATE_MASK;
	uint32_t index;
	uint32_t value;
	int ret;

	if (artinchip_clock_is_qspi(id)) {
		uint32_t mask = config->is_d12p ? BIT(17) | BIT(16) | BIT(21) | BIT(22) :
				AIC_QSPI_CLK_BUS_GATE | AIC_QSPI_CLK_MOD_GATE;

		value = sys_read32(config->base + artinchip_clock_qspi_reg_offset(id));
		if ((value & mask) == mask) {
			return CLOCK_CONTROL_STATUS_ON;
		}

		return CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_GPIO) {
		uint32_t mask = config->is_d12p ? BIT(21) : AIC_GPIO_CLK_BUS_GATE;

		value = sys_read32(config->base + AIC_GPIO_CLK_REG);
		return (value & mask) != 0U ? CLOCK_CONTROL_STATUS_ON :
							       CLOCK_CONTROL_STATUS_OFF;
	}

	if (artinchip_clock_is_i2c(id)) {
		uint32_t gate_mask = artinchip_clock_i2c_gate_mask(config);

		value = sys_read32(config->base + artinchip_clock_i2c_reg_offset(id));
		return (value & gate_mask) != 0U ? CLOCK_CONTROL_STATUS_ON :
						   CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_PWM) {
		value = sys_read32(config->base + AIC_PWM_CLK_REG);
		if ((value & (AIC_PWM_CLK_BUS_GATE | AIC_PWM_CLK_MOD_GATE)) ==
		    (AIC_PWM_CLK_BUS_GATE | AIC_PWM_CLK_MOD_GATE)) {
			return CLOCK_CONTROL_STATUS_ON;
		}

		return CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_XSPI) {
		value = sys_read32(config->base + AIC_XSPI_CLK_REG);
		if ((value & (AIC_XSPI_CLK_BUS_GATE | AIC_XSPI_CLK_MOD_GATE)) ==
		    (AIC_XSPI_CLK_BUS_GATE | AIC_XSPI_CLK_MOD_GATE)) {
			return CLOCK_CONTROL_STATUS_ON;
		}

		return CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_SYSCFG) {
		value = sys_read32(config->base + AIC_SYSCFG_CLK_REG);
		return (value & AIC_SYSCFG_CLK_BUS_GATE) != 0U ? CLOCK_CONTROL_STATUS_ON :
							       CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_RTC) {
		value = sys_read32(config->base + AIC_RTC_CLK_REG);
		return (value & AIC_RTC_CLK_BUS_GATE) != 0U ? CLOCK_CONTROL_STATUS_ON :
							       CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_TSEN) {
		value = sys_read32(config->base + AIC_TSEN_CLK_REG);
		return (value & AIC_TSEN_CLK_BUS_GATE) != 0U ? CLOCK_CONTROL_STATUS_ON :
								CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_RTP) {
		value = sys_read32(config->base + AIC_RTP_CLK_REG);
		return (value & AIC_RTP_CLK_BUS_GATE) != 0U ? CLOCK_CONTROL_STATUS_ON :
							       CLOCK_CONTROL_STATUS_OFF;
	}

	if (artinchip_clock_is_sdmc(id)) {
		uint32_t gate_mask = artinchip_clock_sdmc_gate_mask(config);

		value = sys_read32(config->base + artinchip_clock_sdmc_reg_offset(config, id));
		if ((value & gate_mask) == gate_mask) {
			return CLOCK_CONTROL_STATUS_ON;
		}

		return CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_SPIENC) {
		value = sys_read32(config->base + AIC_SPIENC_CLK_REG);
		if ((value & (AIC_SPIENC_CLK_BUS_GATE | AIC_SPIENC_CLK_MOD_GATE)) ==
		    (AIC_SPIENC_CLK_BUS_GATE | AIC_SPIENC_CLK_MOD_GATE)) {
			return CLOCK_CONTROL_STATUS_ON;
		}

		return CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_WDT) {
		value = sys_read32(config->base + AIC_WDT_CLK_REG);
		if ((value & (AIC_WDT_CLK_BUS_GATE | AIC_WDT_CLK_MOD_GATE)) ==
		    (AIC_WDT_CLK_BUS_GATE | AIC_WDT_CLK_MOD_GATE)) {
			return CLOCK_CONTROL_STATUS_ON;
		}

		return CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_SID) {
		value = sys_read32(config->base + AIC_SID_CLK_REG);
		if ((value & (AIC_SID_CLK_BUS_GATE | AIC_SID_CLK_MOD_GATE)) ==
		    (AIC_SID_CLK_BUS_GATE | AIC_SID_CLK_MOD_GATE)) {
			return CLOCK_CONTROL_STATUS_ON;
		}

		return CLOCK_CONTROL_STATUS_OFF;
	}

	if (artinchip_clock_is_can(id)) {
		value = sys_read32(config->base + artinchip_clock_can_reg_offset(id));
		return (value & AIC_CAN_CLK_BUS_GATE) != 0U ? CLOCK_CONTROL_STATUS_ON :
							      CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_ADCIM) {
		value = sys_read32(config->base + AIC_ADCIM_CLK_REG);
		if ((value & (AIC_ADCIM_CLK_BUS_GATE | AIC_ADCIM_CLK_MOD_GATE)) ==
		    (AIC_ADCIM_CLK_BUS_GATE | AIC_ADCIM_CLK_MOD_GATE)) {
			return CLOCK_CONTROL_STATUS_ON;
		}

		return CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_GPAI) {
		value = sys_read32(config->base + AIC_GPAI_CLK_REG);
		return (value & AIC_GPAI_CLK_BUS_GATE) != 0U ? CLOCK_CONTROL_STATUS_ON :
							      CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_PSADC) {
		value = sys_read32(config->base + AIC_PSADC_CLK_REG);
		if ((value & (AIC_PSADC_CLK_BUS_GATE | AIC_PSADC_CLK_MOD_GATE)) ==
		    (AIC_PSADC_CLK_BUS_GATE | AIC_PSADC_CLK_MOD_GATE)) {
			return CLOCK_CONTROL_STATUS_ON;
		}

		return CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_CIR) {
		value = sys_read32(config->base + AIC_CIR_CLK_REG);
		return (value & AIC_CIR_CLK_BUS_GATE) != 0U ? CLOCK_CONTROL_STATUS_ON :
							     CLOCK_CONTROL_STATUS_OFF;
	}

	if ((id == ARTINCHIP_CLK_MTOP) || (id == ARTINCHIP_CLK_PBUS) ||
	    (id == ARTINCHIP_CLK_PWMCS) || (id == ARTINCHIP_CLK_I2S0) ||
	    (id == ARTINCHIP_CLK_CODEC) || (id == ARTINCHIP_CLK_GMAC0) ||
	    (id == ARTINCHIP_CLK_CE) || (id == ARTINCHIP_CLK_GE) ||
	    (id == ARTINCHIP_CLK_VE) || (id == ARTINCHIP_CLK_MIPIDSI) ||
	    (id == ARTINCHIP_CLK_RGB) || (id == ARTINCHIP_CLK_DE) ||
	    (id == ARTINCHIP_CLK_DVP) ||
	    (id == ARTINCHIP_CLK_USBD) || (id == ARTINCHIP_CLK_USBH0) ||
	    (id == ARTINCHIP_CLK_USB_PHY0)) {
		uint32_t reg = id == ARTINCHIP_CLK_MTOP ? AIC_MTOP_CLK_REG :
			       id == ARTINCHIP_CLK_PBUS ? AIC_PBUS_CLK_REG :
			       id == ARTINCHIP_CLK_PWMCS ? AIC_PWMCS_CLK_REG :
			       id == ARTINCHIP_CLK_I2S0 ? AIC_I2S0_CLK_REG :
			       id == ARTINCHIP_CLK_CODEC ? AIC_CODEC_CLK_REG :
			       id == ARTINCHIP_CLK_GMAC0 ? AIC_GMAC0_CLK_REG :
			       id == ARTINCHIP_CLK_CE ? AIC_CE_CLK_REG :
			       id == ARTINCHIP_CLK_GE ? AIC_GE_CLK_REG :
			       id == ARTINCHIP_CLK_VE ? AIC_VE_CLK_REG :
			       id == ARTINCHIP_CLK_MIPIDSI ? AIC_MIPIDSI_CLK_REG :
			       id == ARTINCHIP_CLK_RGB ? AIC_RGB_CLK_REG :
			       id == ARTINCHIP_CLK_DE ? AIC_DE_CLK_REG :
			       id == ARTINCHIP_CLK_DVP ? AIC_DVP_CLK_REG :
			       id == ARTINCHIP_CLK_USBD ? AIC_USBD_CLK_REG :
			       id == ARTINCHIP_CLK_USBH0 ? AIC_USBH0_CLK_REG :
								 AIC_USB_PHY0_CLK_REG;
		uint32_t mask = id == ARTINCHIP_CLK_USB_PHY0 ?
					artinchip_clock_usb_gate_mask(config, id,
								 AIC_USB_PHY0_CLK_GATE_MASK) :
				id == ARTINCHIP_CLK_USBD ?
					artinchip_clock_usb_gate_mask(config, id,
								 AIC_USBD_CLK_GATE_MASK) :
				id == ARTINCHIP_CLK_USBH0 ?
					artinchip_clock_usb_gate_mask(config, id,
								 AIC_USBH0_CLK_GATE_MASK) :
				id == ARTINCHIP_CLK_GE ?
					artinchip_clock_display_gate_mask(config, id,
								     AIC_GE_CLK_GATE_MASK) :
				id == ARTINCHIP_CLK_MIPIDSI ?
					artinchip_clock_display_gate_mask(config, id,
								     config->is_d12p ?
								     AIC_RGB_CLK_GATE_MASK :
								     AIC_MIPIDSI_CLK_GATE_MASK) :
				id == ARTINCHIP_CLK_RGB ?
					artinchip_clock_display_gate_mask(config, id,
								     AIC_RGB_CLK_GATE_MASK) :
				id == ARTINCHIP_CLK_DE ?
					artinchip_clock_display_gate_mask(config, id,
								     AIC_DE_CLK_GATE_MASK) :
				id == ARTINCHIP_CLK_DVP ?
					artinchip_clock_display_gate_mask(config, id,
								     AIC_DVP_CLK_GATE_MASK) :
								 AIC_SIMPLE_CLK_BUS_GATE;

		value = sys_read32(config->base + reg);
		if ((id == ARTINCHIP_CLK_MIPIDSI) && config->is_d12p) {
			value = sys_read32(config->base + AIC_RGB_CLK_REG);
			mask = artinchip_clock_display_gate_mask(config, id, AIC_RGB_CLK_GATE_MASK);
			return (value & mask) == mask ? CLOCK_CONTROL_STATUS_ON :
							CLOCK_CONTROL_STATUS_OFF;
		}

		return (value & mask) == mask ? CLOCK_CONTROL_STATUS_ON : CLOCK_CONTROL_STATUS_OFF;
	}

	if (id == ARTINCHIP_CLK_LVDS) {
		value = sys_read32(config->base + AIC_LVDS_CLK_REG);
		return (value & AIC_LVDS_CLK_GATE_MASK) == AIC_LVDS_CLK_GATE_MASK ?
			CLOCK_CONTROL_STATUS_ON : CLOCK_CONTROL_STATUS_OFF;
	}

	ret = artinchip_clock_uart_index(id, &index);
	if (ret != 0) {
		return CLOCK_CONTROL_STATUS_UNKNOWN;
	}

	value = sys_read32(config->base + artinchip_clock_uart_reg_offset(index));

	if ((value & uart_gate_mask) == uart_gate_mask) {
		return CLOCK_CONTROL_STATUS_ON;
	}

	return CLOCK_CONTROL_STATUS_OFF;
}

static int artinchip_clock_set_rate(const struct device *dev, clock_control_subsys_t sys,
			       clock_control_subsys_rate_t rate)
{
	const struct artinchip_clock_config *config = dev->config;
	struct artinchip_clock_data *data = dev->data;
	uint32_t index;
	enum clock_control_status status;
	uint32_t hz;
	int ret;

	if (rate == NULL) {
		return -EINVAL;
	}

	hz = *(const uint32_t *)rate;

	ret = artinchip_clock_uart_index(POINTER_TO_UINT(sys), &index);
	if (ret != 0) {
		return ret;
	}

	if ((hz == 0U) || (hz > artinchip_clock_uart_parent_rate(config))) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		data->uart_rates[index] = hz;
	}

	status = artinchip_clock_get_status(dev, sys);
	if (status == CLOCK_CONTROL_STATUS_ON) {
		return artinchip_clock_apply_uart_rate(dev, index, true);
	}

	return 0;
}

static DEVICE_API(clock_control, artinchip_clock_api) = {
	.on = artinchip_clock_on,
	.off = artinchip_clock_off,
	.get_rate = artinchip_clock_get_rate,
	.get_status = artinchip_clock_get_status,
	.set_rate = artinchip_clock_set_rate,
};

static const struct artinchip_clock_config artinchip_clock_config = {
	.base = DT_REG_ADDR(DT_INST_PARENT(0)),
	.parent_rate = DT_INST_PROP(0, clock_frequency),
	.default_uart_rate = DT_INST_PROP_OR(0, artinchip_uart0_clock_frequency,
					      AIC_UART_CLK_DEFAULT_HZ),
	.qspi_rate = DT_INST_PROP_OR(0, artinchip_qspi_clock_frequency,
				     AIC_QSPI_CLK_DEFAULT_HZ),
	.apb0_rate = DT_INST_PROP_OR(0, artinchip_apb0_clock_frequency,
				     AIC_APB0_CLK_DEFAULT_HZ),
	.apb1_rate = DT_INST_PROP_OR(0, artinchip_apb1_clock_frequency,
				     AIC_APB1_CLK_DEFAULT_HZ),
	.pwm_rate = DT_INST_PROP_OR(0, artinchip_pwm_clock_frequency,
				    AIC_PWM_CLK_DEFAULT_HZ),
	.xspi_rate = DT_INST_PROP_OR(0, artinchip_xspi_clock_frequency,
				     AIC_XSPI_CLK_DEFAULT_HZ),
	.display_sclk_rate = DT_INST_PROP_OR(0, artinchip_display_sclk_clock_frequency,
					      AIC_V1_DISPLAY_SCLK_DEFAULT_HZ),
	.display_pixclk_rate = DT_INST_PROP_OR(0, artinchip_display_pixclk_clock_frequency,
						AIC_V1_DISPLAY_PIXCLK_DEFAULT_HZ),
	.is_d12p = DT_INST_NODE_HAS_COMPAT(0, artinchip_d12p_cctl),
	.is_d21x = DT_INST_NODE_HAS_COMPAT(0, artinchip_d21x_cctl),
};

static struct artinchip_clock_data artinchip_clock_data;

DEVICE_DT_INST_DEFINE(0, NULL, NULL, &artinchip_clock_data, &artinchip_clock_config, PRE_KERNEL_1,
		      CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &artinchip_clock_api);
