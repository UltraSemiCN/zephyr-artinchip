/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT artinchip_d13x_mipi_dsi

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/artinchip-clock.h>
#include <zephyr/drivers/mipi_dsi.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <string.h>

#if defined(CONFIG_MIPI_DSI_ARTINCHIP_SHELL)
#include <stdlib.h>
#include <zephyr/shell/shell.h>

#if DT_HAS_COMPAT_STATUS_OKAY(artinchip_d12p_cctl)
#define AIC_DSI_CMU_BASE DT_REG_ADDR(DT_PARENT(DT_INST_CLOCKS_CTLR(0)))
#define AIC_DSI_PLL_FRA2_GEN_REG  0x0028U
#define AIC_DSI_PLL_FRA2_CFG_REG  0x0068U
#define AIC_DSI_PLL_FRA2_SDM_REG  0x0088U
#define AIC_DSI_DISP_SCLK_REG     0x0220U
#define AIC_DSI_DISP_PIXCLK_REG   0x0224U
#endif
#endif

LOG_MODULE_REGISTER(dsi_artinchip, CONFIG_MIPI_DSI_LOG_LEVEL);

#define DSI_CTL          0x00U
#define DSI_CLK_CFG      0x04U
#define DSI_DPI_IN_POL   0x10U
#define DSI_DPI_IN_FMT   0x14U
#define DSI_DPI_VC       0x18U
#define DSI_DPI_LPTX_TIME 0x1CU
#define DSI_GEN_PH_CFG   0x20U
#define DSI_GEN_PD_CFG   0x24U
#define DSI_VID_MODE_CFG 0x30U
#define DSI_VID_PKG_SIZE 0x34U
#define DSI_VID_CHK_NUM  0x38U
#define DSI_VID_NULL_SIZE 0x3CU
#define DSI_VID_HINACT_TIME 0x40U
#define DSI_VID_HT_TIME  0x44U
#define DSI_VID_VBLANK_LINE 0x48U
#define DSI_VID_VACT_LINE 0x4CU
#define DSI_CMD_MODE_CFG 0x50U
#define DSI_CMD_PKG_STA  0x58U
#define DSI_EDPI_CMD_SIZE 0x54U
#define DSI_IRQ_EN1      0x60U
#define DSI_IRQ_EN2      0x64U
#define DSI_IRQ_STA1     0x68U
#define DSI_IRQ_STA2     0x6CU
#define DSI_TO_CNT_CFG   0x70U
#define DSI_PHY_CFG      0x80U
#define DSI_PHY_STA      0x84U
#define DSI_PHY_CLK_TIME 0x88U
#define DSI_PHY_DATA_TIME 0x8CU
#define DSI_PHY_RD_TIME  0x90U
#define DSI_PHY_TEST1    0xC0U
#define DSI_PHY_TEST2    0xC4U
#define DSI_DPI_IF_CFG   0x400U
#define DSI_LANE_CFG     0x410U
#define DSI_ANA_CFG1     0x500U
#define DSI_ANA_CFG2     0x504U

#define DSI_CTL_PKG_CFG_CRC_RX_EN  BIT(7)
#define DSI_CTL_PKG_CFG_ECC_RX_EN  BIT(6)
#define DSI_CTL_PKG_CFG_BTA_EN      BIT(5)
#define DSI_CTL_PKG_CFG_EOTP_TX_EN  BIT(3)
#define DSI_CTL_DSI_MODE            BIT(1)
#define DSI_CTL_EN                  BIT(0)
#define DSI_CLK_CFG_TO_DIV_MASK     GENMASK(15, 8)
#define DSI_CLK_CFG_TO_DIV(x)       FIELD_PREP(DSI_CLK_CFG_TO_DIV_MASK, (x))
#define DSI_CLK_CFG_LP_DIV_MASK     GENMASK(7, 0)
#define DSI_CLK_CFG_LP_DIV(x)       FIELD_PREP(DSI_CLK_CFG_LP_DIV_MASK, (x))
#define DSI_DPI_VC_NUM_MASK         GENMASK(1, 0)
#define DSI_DPI_VC_NUM(x)           FIELD_PREP(DSI_DPI_VC_NUM_MASK, (x))
#define DSI_DPI_IN_FMT_LOOSELY18    BIT(8)
#define DSI_DPI_IN_FMT_DT_MASK      GENMASK(3, 0)
#define DSI_DPI_IN_FMT_DT(x)        FIELD_PREP(DSI_DPI_IN_FMT_DT_MASK, (x))
#define DSI_DPI_IN_POL_HSYNC        BIT(2)
#define DSI_DPI_IN_POL_VSYNC        BIT(1)
#define DSI_DPI_IN_POL_DE           BIT(0)
#define DSI_DPI_IN_POL_COLORM       BIT(4)
#define DSI_DPI_IN_POL_SHUTDOWN     BIT(3)
#define DSI_DPI_LPTX_TIME_OUTVACT_MASK GENMASK(24, 16)
#define DSI_DPI_LPTX_TIME_OUTVACT(x) FIELD_PREP(DSI_DPI_LPTX_TIME_OUTVACT_MASK, (x))
#define DSI_DPI_LPTX_TIME_INVACT_MASK GENMASK(7, 0)
#define DSI_DPI_LPTX_TIME_INVACT(x) FIELD_PREP(DSI_DPI_LPTX_TIME_INVACT_MASK, (x))
#define DSI_DPI_IF_CFG_FMT_MASK     GENMASK(1, 0)
#define DSI_DPI_IF_CFG_FMT(x)       FIELD_PREP(DSI_DPI_IF_CFG_FMT_MASK, (x))
#define DSI_DPI_IF_CFG_SHUTD        BIT(2)
#define DSI_DPI_IF_CFG_COLORM       BIT(3)
#define DSI_ANA_CFG1_DATA_LANE_POL_MASK GENMASK(19, 16)
#define DSI_ANA_CFG1_DATA_LANE_POL(x)   FIELD_PREP(DSI_ANA_CFG1_DATA_LANE_POL_MASK, (x))
#define DSI_ANA_CFG1_DATA_CLK_POL   BIT(15)
#define DSI_LANE_POLARITIES         0U
#define DSI_CLK_INVERSE             0U
#define DSI_VID_MODE_CFG_CMD_LPTX_FORCE BIT(15)
#define DSI_VID_MODE_CFG_FRAME_BTA_ACK_EN BIT(14)
#define DSI_VID_MODE_CFG_LP_EN_HFP  BIT(13)
#define DSI_VID_MODE_CFG_LP_EN_HBP  BIT(12)
#define DSI_VID_MODE_CFG_LP_EN_VACT BIT(11)
#define DSI_VID_MODE_CFG_LP_EN_VFP  BIT(10)
#define DSI_VID_MODE_CFG_LP_EN_VBP  BIT(9)
#define DSI_VID_MODE_CFG_LP_EN_VSA  BIT(8)
#define DSI_VID_MODE_CFG_TYPE_MASK  GENMASK(1, 0)
#define DSI_VID_MODE_CFG_TYPE(x)    FIELD_PREP(DSI_VID_MODE_CFG_TYPE_MASK, (x))
#define DSI_VID_HBP_TIME_MASK       GENMASK(27, 16)
#define DSI_VID_HBP_TIME(x)         FIELD_PREP(DSI_VID_HBP_TIME_MASK, (x))
#define DSI_VID_HSA_TIME_MASK       GENMASK(11, 0)
#define DSI_VID_HSA_TIME(x)         FIELD_PREP(DSI_VID_HSA_TIME_MASK, (x))
#define DSI_VID_VFP_LINE_MASK       GENMASK(25, 16)
#define DSI_VID_VFP_LINE(x)         FIELD_PREP(DSI_VID_VFP_LINE_MASK, (x))
#define DSI_VID_VBP_TIME_MASK       GENMASK(9, 0)
#define DSI_VID_VBP_TIME(x)         FIELD_PREP(DSI_VID_VBP_TIME_MASK, (x))
#define DSI_VID_VSA_LINE_MASK       GENMASK(25, 16)
#define DSI_VID_VSA_LINE(x)         FIELD_PREP(DSI_VID_VSA_LINE_MASK, (x))
#define DSI_VID_VACT_TIME_MASK      GENMASK(13, 0)
#define DSI_VID_VACT_TIME(x)        FIELD_PREP(DSI_VID_VACT_TIME_MASK, (x))
#define DSI_CMD_MODE_CFG_MAX_RD_PKG_SIZE BIT(24)
#define DSI_CMD_MODE_CFG_DCS_LW     BIT(19)
#define DSI_CMD_MODE_CFG_DCS_SR_0P  BIT(18)
#define DSI_CMD_MODE_CFG_DCS_SW_1P  BIT(17)
#define DSI_CMD_MODE_CFG_DCS_SW_0P  BIT(16)
#define DSI_CMD_MODE_CFG_GEN_LW     BIT(14)
#define DSI_CMD_MODE_CFG_GEN_SR_2P  BIT(13)
#define DSI_CMD_MODE_CFG_GEN_SR_1P  BIT(12)
#define DSI_CMD_MODE_CFG_GEN_SR_0P  BIT(11)
#define DSI_CMD_MODE_CFG_GEN_SW_2P  BIT(10)
#define DSI_CMD_MODE_CFG_GEN_SW_1P  BIT(9)
#define DSI_CMD_MODE_CFG_GEN_SW_0P  BIT(8)
#define DSI_CMD_MODE_CFG_ACK_REQ_EN BIT(1)
#define DSI_CMD_MODE_CFG_TE_EN      BIT(0)
#define DSI_TO_CNT_CFG_HSTX_MASK    GENMASK(31, 16)
#define DSI_TO_CNT_CFG_HSTX(x)      FIELD_PREP(DSI_TO_CNT_CFG_HSTX_MASK, (x))
#define DSI_TO_CNT_CFG_LPRX_MASK    GENMASK(15, 0)
#define DSI_TO_CNT_CFG_LPRX(x)      FIELD_PREP(DSI_TO_CNT_CFG_LPRX_MASK, (x))
#define DSI_CMD_PKG_STA_PLD_W_EMPTY BIT(2)
#define DSI_CMD_PKG_STA_CMD_EMPTY   BIT(0)
#define DSI_CMD_PKG_STA_RD_CMD_BUSY BIT(6)
#define DSI_CMD_PKG_STA_PLD_W_FULL  BIT(3)
#define DSI_CMD_PKG_STA_CMD_FULL    BIT(1)
#define DSI_IRQ_STA2_DPI_FIFO_OV    BIT(7)
#define DSI_PHY_CLK_TIME_HS2LP_MASK GENMASK(25, 16)
#define DSI_PHY_CLK_TIME_HS2LP(x)   FIELD_PREP(DSI_PHY_CLK_TIME_HS2LP_MASK, (x))
#define DSI_PHY_CLK_TIME_LP2HS_MASK GENMASK(9, 0)
#define DSI_PHY_CLK_TIME_LP2HS(x)   FIELD_PREP(DSI_PHY_CLK_TIME_LP2HS_MASK, (x))
#define DSI_PHY_DATA_TIME_HS2LP_MASK GENMASK(25, 16)
#define DSI_PHY_DATA_TIME_HS2LP(x)  FIELD_PREP(DSI_PHY_DATA_TIME_HS2LP_MASK, (x))
#define DSI_PHY_DATA_TIME_LP2HS_MASK GENMASK(9, 0)
#define DSI_PHY_DATA_TIME_LP2HS(x)  FIELD_PREP(DSI_PHY_DATA_TIME_LP2HS_MASK, (x))
#define DSI_PHY_CFG_RST_CLK_EN      BIT(6)
#define DSI_PHY_CFG_RST_RSTN        BIT(5)
#define DSI_PHY_CFG_RST_SHUTDOWNZ   BIT(4)
#define DSI_PHY_CFG_AUTO_CLK_EN     BIT(3)
#define DSI_PHY_CFG_HSCLK_REQ       BIT(2)
#define DSI_PHY_CFG_LP11_TIME_MASK  GENMASK(15, 8)
#define DSI_PHY_CFG_LP11_TIME(x)    FIELD_PREP(DSI_PHY_CFG_LP11_TIME_MASK, (x))
#define DSI_PHY_CFG_DATA_LANE_MASK  GENMASK(1, 0)
#define DSI_PHY_CFG_DATA_LANE(x)    FIELD_PREP(DSI_PHY_CFG_DATA_LANE_MASK, (x))
#define DSI_PHY_STA_STOP_STATE_0    BIT(4)
#define DSI_PHY_STA_STOP_STATE_C    BIT(2)
#define DSI_PHY_STA_PHY_LOCK        BIT(0)
/* Match LubanLite dsi_phy_stop_mask(): clock lane + data lane 0 in LP-11 stop. */
#define DSI_PHY_STA_STOP_MASK       (DSI_PHY_STA_STOP_STATE_C | DSI_PHY_STA_STOP_STATE_0)
#define DSI_PHY_TEST1_CLK           BIT(1)
#define DSI_PHY_TEST1_CLR           BIT(0)
#define DSI_PHY_TEST2_EN            BIT(16)
#define DSI_PHY_TEST2_DIN_MASK      GENMASK(7, 0)
#define DSI_PHY_TEST2_DIN(x)        FIELD_PREP(DSI_PHY_TEST2_DIN_MASK, (x))
#define DSI_ANA_CFG2_EN_CLK         BIT(12)
#define DSI_ANA_CFG2_RCAL_FLAG      BIT(10)
#define DSI_ANA_CFG2_ON_RESCAL      BIT(9)
#define DSI_ANA_CFG2_EN_RESCAL      BIT(8)
#define DSI_ANA_CFG2_EN_LDO_MASK    GENMASK(7, 3)
#define DSI_ANA_CFG2_EN_LDO(x)      FIELD_PREP(DSI_ANA_CFG2_EN_LDO_MASK, (x))
#define DSI_ANA_CFG2_EN_VP_MASK     GENMASK(2, 1)
#define DSI_ANA_CFG2_EN_VP(x)       FIELD_PREP(DSI_ANA_CFG2_EN_VP_MASK, (x))
#define DSI_ANA_CFG2_EN_BIAS        BIT(0)

#define DSI_DPI_DT_RGB565           0x01U
#define DSI_DPI_DT_RGB666           0x03U
#define DSI_DPI_DT_RGB888           0x05U

#define DSI_MAX_LANES               4U
#define DSI_PHY_TIMEOUT_US          5000U
#define DSI_PHY_LP_TIMEOUT_US       2000U
#define DSI_CMD_GUARD_TIME_US       100U
#define DSI_LP_RATE_HZ              10000000U
#define DSI_LANE_ASSIGNMENTS        0x3210U
#define DSI_CMD_MODE_CFG_SUPPORTED  (DSI_CMD_MODE_CFG_MAX_RD_PKG_SIZE | \
				     DSI_CMD_MODE_CFG_DCS_LW | \
				     DSI_CMD_MODE_CFG_DCS_SR_0P | \
				     DSI_CMD_MODE_CFG_DCS_SW_1P | \
				     DSI_CMD_MODE_CFG_DCS_SW_0P | \
				     DSI_CMD_MODE_CFG_GEN_LW | \
				     DSI_CMD_MODE_CFG_GEN_SR_2P | \
				     DSI_CMD_MODE_CFG_GEN_SR_1P | \
				     DSI_CMD_MODE_CFG_GEN_SR_0P | \
				     DSI_CMD_MODE_CFG_GEN_SW_2P | \
				     DSI_CMD_MODE_CFG_GEN_SW_1P | \
				     DSI_CMD_MODE_CFG_GEN_SW_0P)
#define DSI_CTL_PACKET_ENABLES      (DSI_CTL_PKG_CFG_CRC_RX_EN | \
				     DSI_CTL_PKG_CFG_ECC_RX_EN | \
				     DSI_CTL_PKG_CFG_EOTP_TX_EN | \
				     DSI_CTL_EN)

struct artinchip_dsi_config {
	uintptr_t base;
	uint32_t phy_clock;
	bool manual_phy_start;
	const struct pinctrl_dev_config *pincfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_id;
	struct reset_dt_spec reset;
};

struct artinchip_dsi_data {
	bool powered;
	bool phy_initialized;
	bool attached;
	uint8_t channel;
	uint8_t data_lanes;
	uint32_t pixfmt;
	uint32_t mode_flags;
	uint32_t lane_rate;
	struct mipi_dsi_timings timings;
};

static uint32_t artinchip_dsi_read(const struct artinchip_dsi_config *config, uint32_t offset)
{
	return sys_read32(config->base + offset);
}

static void artinchip_dsi_write(const struct artinchip_dsi_config *config, uint32_t offset,
				uint32_t value)
{
	sys_write32(value, config->base + offset);
}

static int artinchip_dsi_bpp(uint32_t pixfmt)
{
	switch (pixfmt) {
	case MIPI_DSI_PIXFMT_RGB888:
		return 24;
	case MIPI_DSI_PIXFMT_RGB666:
	case MIPI_DSI_PIXFMT_RGB666_PACKED:
		return 18;
	case MIPI_DSI_PIXFMT_RGB565:
		return 16;
	default:
		return -EINVAL;
	}
}

static int artinchip_dsi_pixel_dt(uint32_t pixfmt)
{
	switch (pixfmt) {
	case MIPI_DSI_PIXFMT_RGB888:
		return DSI_DPI_DT_RGB888;
	case MIPI_DSI_PIXFMT_RGB666:
	case MIPI_DSI_PIXFMT_RGB666_PACKED:
		return DSI_DPI_DT_RGB666;
	case MIPI_DSI_PIXFMT_RGB565:
		return DSI_DPI_DT_RGB565;
	default:
		return -EINVAL;
	}
}

static uint32_t artinchip_dsi_if_cfg_fmt(uint32_t pixfmt)
{
	/* DSI_DPI_IF_CFG format field mapping for DSI_FMT_* order. */
	switch (pixfmt) {
	case MIPI_DSI_PIXFMT_RGB888:
		return 0U;
	case MIPI_DSI_PIXFMT_RGB666:
	case MIPI_DSI_PIXFMT_RGB666_PACKED:
		return 1U;
	case MIPI_DSI_PIXFMT_RGB565:
		return 2U;
	default:
		return 0U;
	}
}

static void artinchip_dsi_update_bits(const struct artinchip_dsi_config *config,
				      uint32_t offset, uint32_t mask, uint32_t value)
{
	uint32_t reg = artinchip_dsi_read(config, offset);

	reg &= ~mask;
	reg |= value & mask;
	artinchip_dsi_write(config, offset, reg);
}

static int artinchip_dsi_wait_bits_timeout(const struct artinchip_dsi_config *config,
					   uint32_t offset, uint32_t mask, uint32_t value,
					   uint32_t timeout_us)
{
	for (uint32_t elapsed = 0U; elapsed < timeout_us; elapsed++) {
		if ((artinchip_dsi_read(config, offset) & mask) == value) {
			return 0;
		}
		k_busy_wait(1);
	}

	return -ETIMEDOUT;
}

static void artinchip_dsi_dphy_test_code(const struct artinchip_dsi_config *config,
					 uint8_t code)
{
	artinchip_dsi_update_bits(config, DSI_PHY_TEST2, DSI_PHY_TEST2_EN,
				  DSI_PHY_TEST2_EN);
	k_busy_wait(1);
	artinchip_dsi_update_bits(config, DSI_PHY_TEST1, DSI_PHY_TEST1_CLK,
				  DSI_PHY_TEST1_CLK);
	k_busy_wait(1);
	artinchip_dsi_update_bits(config, DSI_PHY_TEST2, DSI_PHY_TEST2_DIN_MASK,
				  DSI_PHY_TEST2_DIN(code));
	k_busy_wait(1);
	artinchip_dsi_update_bits(config, DSI_PHY_TEST1, DSI_PHY_TEST1_CLK, 0U);
	k_busy_wait(1);
	artinchip_dsi_update_bits(config, DSI_PHY_TEST2, DSI_PHY_TEST2_EN, 0U);
	k_busy_wait(1);
}

static void artinchip_dsi_dphy_set_hsfreq(const struct artinchip_dsi_config *config,
					  uint32_t lane_rate)
{
	static const struct {
		uint16_t max_mhz;
		uint8_t value;
	} hs_clk_div[] = {
		{ 89, 0x00 },   { 99, 0x10 },   { 109, 0x20 },  { 129, 0x01 },
		{ 139, 0x11 },  { 149, 0x21 },  { 169, 0x02 },  { 179, 0x12 },
		{ 199, 0x22 },  { 219, 0x03 },  { 239, 0x13 },  { 249, 0x23 },
		{ 269, 0x04 },  { 299, 0x14 },  { 329, 0x05 },  { 359, 0x15 },
		{ 399, 0x25 },  { 449, 0x06 },  { 499, 0x16 },  { 549, 0x07 },
		{ 599, 0x17 },  { 649, 0x08 },  { 699, 0x18 },  { 749, 0x09 },
		{ 799, 0x19 },  { 849, 0x29 },  { 899, 0x39 },  { 949, 0x0a },
		{ 999, 0x1a },  { 1049, 0x2a }, { 1099, 0x3a }, { 1149, 0x0b },
		{ 1199, 0x1b }, { 1249, 0x2b }, { 1299, 0x3b }, { 1349, 0x0c },
		{ 1399, 0x1c }, { 1449, 0x2c }, { 1499, 0x3c },
	};
	uint32_t mhz = DIV_ROUND_UP(lane_rate, 1000000U);
	uint8_t value = hs_clk_div[ARRAY_SIZE(hs_clk_div) - 1].value;

	for (size_t i = 0U; i < ARRAY_SIZE(hs_clk_div); i++) {
		if (hs_clk_div[i].max_mhz >= mhz) {
			value = hs_clk_div[i].value;
			break;
		}
	}

	artinchip_dsi_dphy_test_code(config, 0x44U);
	artinchip_dsi_update_bits(config, DSI_PHY_TEST2, DSI_PHY_TEST2_DIN_MASK,
				  DSI_PHY_TEST2_DIN(value));
	k_busy_wait(1);
	artinchip_dsi_update_bits(config, DSI_PHY_TEST1, DSI_PHY_TEST1_CLK,
				  DSI_PHY_TEST1_CLK);
	k_busy_wait(1);
	artinchip_dsi_update_bits(config, DSI_PHY_TEST1, DSI_PHY_TEST1_CLK, 0U);
	k_busy_wait(1);
}

static uint32_t artinchip_dsi_video_mode_type(uint32_t mode_flags)
{
	if ((mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) != 0U) {
		return 0U;
	}

	if ((mode_flags & MIPI_DSI_MODE_VIDEO_BURST) != 0U) {
		return 2U;
	}

	return 1U;
}

static void artinchip_dsi_configure_timings(const struct device *dev,
					    const struct mipi_dsi_device *mdev, uint32_t bpp)
{
	const struct artinchip_dsi_config *config = dev->config;
	const struct mipi_dsi_timings *timings = &mdev->timings;
	uint32_t htotal;
	uint32_t divisor = 8U * mdev->data_lanes;
	uint32_t value;

	if ((mdev->mode_flags & MIPI_DSI_MODE_VIDEO) == 0U) {
		return;
	}

	htotal = timings->hbp + timings->hsync + timings->hactive + timings->hfp;

	value = DSI_VID_MODE_CFG_LP_EN_VSA | DSI_VID_MODE_CFG_LP_EN_VBP |
		DSI_VID_MODE_CFG_LP_EN_VFP | DSI_VID_MODE_CFG_LP_EN_VACT |
		DSI_VID_MODE_CFG_CMD_LPTX_FORCE |
		DSI_VID_MODE_CFG_TYPE(artinchip_dsi_video_mode_type(mdev->mode_flags));
	if ((mdev->mode_flags & MIPI_DSI_MODE_VIDEO_HBP) == 0U) {
		value |= DSI_VID_MODE_CFG_LP_EN_HBP;
	}
	if ((mdev->mode_flags & MIPI_DSI_MODE_VIDEO_BURST) != 0U) {
		value |= DSI_VID_MODE_CFG_LP_EN_HFP;
	}
	artinchip_dsi_write(config, DSI_VID_MODE_CFG, value);

	artinchip_dsi_write(config, DSI_VID_PKG_SIZE, timings->hactive);
	artinchip_dsi_write(config, DSI_VID_CHK_NUM, 1U);
	artinchip_dsi_write(config, DSI_VID_NULL_SIZE, 0U);
	artinchip_dsi_write(config, DSI_VID_HINACT_TIME,
			    DSI_VID_HSA_TIME((timings->hsync * bpp) / divisor) |
			    DSI_VID_HBP_TIME((timings->hbp * bpp) / divisor));
	artinchip_dsi_write(config, DSI_VID_HT_TIME, (htotal * bpp) / divisor);
	artinchip_dsi_write(config, DSI_VID_VBLANK_LINE,
			    DSI_VID_VFP_LINE(timings->vfp) | DSI_VID_VBP_TIME(timings->vbp));
	artinchip_dsi_write(config, DSI_VID_VACT_LINE,
			    DSI_VID_VSA_LINE(timings->vsync) |
			    DSI_VID_VACT_TIME(timings->vactive));
}

static int artinchip_dsi_phy_init(const struct device *dev, bool analog)
{
	const struct artinchip_dsi_config *config = dev->config;
	struct artinchip_dsi_data *data = dev->data;
	uint8_t data_lanes = (data->data_lanes == 0U) ? DSI_MAX_LANES : data->data_lanes;
	uint32_t lane_rate = (data->lane_rate == 0U) ? config->phy_clock : data->lane_rate;
	uint32_t lp_div;
	int ret;

	if (data->phy_initialized) {
		return 0;
	}

	lp_div = lane_rate / (8U * DSI_LP_RATE_HZ);
	if (lp_div == 0U) {
		lp_div = 1U;
	}

	artinchip_dsi_update_bits(config, DSI_CLK_CFG,
				  DSI_CLK_CFG_TO_DIV_MASK | DSI_CLK_CFG_LP_DIV_MASK,
				  DSI_CLK_CFG_TO_DIV(lp_div) | DSI_CLK_CFG_LP_DIV(lp_div));
	artinchip_dsi_update_bits(config, DSI_CTL,
				  DSI_CTL_PKG_CFG_CRC_RX_EN | DSI_CTL_PKG_CFG_ECC_RX_EN |
				  DSI_CTL_PKG_CFG_EOTP_TX_EN,
				  DSI_CTL_PKG_CFG_CRC_RX_EN | DSI_CTL_PKG_CFG_ECC_RX_EN |
				  DSI_CTL_PKG_CFG_EOTP_TX_EN);
	artinchip_dsi_update_bits(config, DSI_DPI_LPTX_TIME,
				  DSI_DPI_LPTX_TIME_OUTVACT_MASK |
				  DSI_DPI_LPTX_TIME_INVACT_MASK,
				  DSI_DPI_LPTX_TIME_OUTVACT(64U) |
				  DSI_DPI_LPTX_TIME_INVACT(64U));
	artinchip_dsi_update_bits(config, DSI_TO_CNT_CFG,
				  DSI_TO_CNT_CFG_HSTX_MASK | DSI_TO_CNT_CFG_LPRX_MASK,
				  DSI_TO_CNT_CFG_HSTX(0U) | DSI_TO_CNT_CFG_LPRX(0U));
	artinchip_dsi_write(config, DSI_CMD_MODE_CFG, DSI_CMD_MODE_CFG_SUPPORTED);

	artinchip_dsi_write(config, DSI_LANE_CFG, DSI_LANE_ASSIGNMENTS);
	artinchip_dsi_update_bits(config, DSI_ANA_CFG1,
				  DSI_ANA_CFG1_DATA_LANE_POL_MASK |
				  DSI_ANA_CFG1_DATA_CLK_POL,
				  DSI_ANA_CFG1_DATA_LANE_POL(DSI_LANE_POLARITIES) |
				  (DSI_CLK_INVERSE ? DSI_ANA_CFG1_DATA_CLK_POL : 0U));
	artinchip_dsi_write(config, DSI_PHY_RD_TIME, 50U);
	artinchip_dsi_update_bits(config, DSI_PHY_CLK_TIME,
				  DSI_PHY_CLK_TIME_HS2LP_MASK |
				  DSI_PHY_CLK_TIME_LP2HS_MASK,
				  DSI_PHY_CLK_TIME_HS2LP(20U) |
				  DSI_PHY_CLK_TIME_LP2HS(20U));
	artinchip_dsi_update_bits(config, DSI_PHY_DATA_TIME,
				  DSI_PHY_DATA_TIME_HS2LP_MASK |
				  DSI_PHY_DATA_TIME_LP2HS_MASK,
				  DSI_PHY_DATA_TIME_HS2LP(20U) |
				  DSI_PHY_DATA_TIME_LP2HS(20U));

	if (analog) {
		artinchip_dsi_update_bits(config, DSI_ANA_CFG2, DSI_ANA_CFG2_EN_BIAS,
					  DSI_ANA_CFG2_EN_BIAS);
		k_busy_wait(5);
		artinchip_dsi_update_bits(config, DSI_ANA_CFG2,
					  DSI_ANA_CFG2_EN_VP_MASK | DSI_ANA_CFG2_EN_LDO_MASK,
					  DSI_ANA_CFG2_EN_VP(3U) | DSI_ANA_CFG2_EN_LDO(0x1fU));
		k_busy_wait(10);
		artinchip_dsi_update_bits(config, DSI_ANA_CFG2,
					  DSI_ANA_CFG2_EN_RESCAL | DSI_ANA_CFG2_ON_RESCAL,
					  DSI_ANA_CFG2_EN_RESCAL | DSI_ANA_CFG2_ON_RESCAL);
		ret = artinchip_dsi_wait_bits_timeout(config, DSI_ANA_CFG2,
						      DSI_ANA_CFG2_RCAL_FLAG,
						      DSI_ANA_CFG2_RCAL_FLAG,
						      DSI_PHY_TIMEOUT_US);
		if (ret != 0) {
			LOG_WRN("timeout waiting DSI RCAL flag, ana2=0x%08x",
				artinchip_dsi_read(config, DSI_ANA_CFG2));
		}

		artinchip_dsi_update_bits(config, DSI_ANA_CFG2, DSI_ANA_CFG2_EN_CLK,
					  DSI_ANA_CFG2_EN_CLK);
	}

	artinchip_dsi_update_bits(config, DSI_PHY_TEST1, DSI_PHY_TEST1_CLR,
				  DSI_PHY_TEST1_CLR);
	k_busy_wait(5);
	artinchip_dsi_update_bits(config, DSI_PHY_CFG, DSI_PHY_CFG_RST_RSTN, 0U);
	k_busy_wait(5);
	artinchip_dsi_update_bits(config, DSI_PHY_CFG, DSI_PHY_CFG_RST_SHUTDOWNZ, 0U);
	k_busy_wait(5);
	artinchip_dsi_update_bits(config, DSI_PHY_TEST1, DSI_PHY_TEST1_CLR, 0U);
	k_busy_wait(5);

	if (analog) {
		artinchip_dsi_dphy_set_hsfreq(config, lane_rate);
		artinchip_dsi_dphy_test_code(config, 0U);
	}
	artinchip_dsi_update_bits(config, DSI_PHY_CFG, DSI_PHY_CFG_RST_SHUTDOWNZ,
				  DSI_PHY_CFG_RST_SHUTDOWNZ);
	k_busy_wait(5);
	artinchip_dsi_update_bits(config, DSI_PHY_CFG, DSI_PHY_CFG_RST_RSTN,
				  DSI_PHY_CFG_RST_RSTN);
	k_busy_wait(5);
	artinchip_dsi_update_bits(config, DSI_PHY_CFG,
				  DSI_PHY_CFG_LP11_TIME_MASK | DSI_PHY_CFG_DATA_LANE_MASK,
				  DSI_PHY_CFG_LP11_TIME(10U) |
				  DSI_PHY_CFG_DATA_LANE(data_lanes - 1U));
	k_busy_wait(5);
	artinchip_dsi_update_bits(config, DSI_PHY_CFG, DSI_PHY_CFG_RST_CLK_EN,
				  DSI_PHY_CFG_RST_CLK_EN);

	if (!config->manual_phy_start) {
		artinchip_dsi_update_bits(config, DSI_PHY_CFG, DSI_PHY_CFG_HSCLK_REQ,
					  DSI_PHY_CFG_HSCLK_REQ);
	}
	data->phy_initialized = true;
	return 0;
}

static int artinchip_dsi_power_on(const struct device *dev)
{
	const struct artinchip_dsi_config *config = dev->config;
	struct artinchip_dsi_data *data = dev->data;
	int ret;

	if (data->powered) {
		return 0;
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

	artinchip_dsi_write(config, DSI_IRQ_EN1, 0U);
	artinchip_dsi_write(config, DSI_IRQ_EN2, 0U);
	artinchip_dsi_update_bits(config, DSI_CTL, DSI_CTL_EN, DSI_CTL_EN);
	data->powered = true;

	return 0;
}

static int artinchip_dsi_power_off(const struct device *dev)
{
	const struct artinchip_dsi_config *config = dev->config;
	struct artinchip_dsi_data *data = dev->data;
	uint32_t ctl;

	if (!data->powered) {
		return 0;
	}

	ctl = artinchip_dsi_read(config, DSI_CTL);
	artinchip_dsi_write(config, DSI_CTL, ctl & ~DSI_CTL_EN);
	(void)reset_line_assert_dt(&config->reset);
	(void)clock_control_off(config->clock_dev, config->clock_id);
	data->powered = false;
	data->phy_initialized = false;
	data->attached = false;

	return 0;
}

static int artinchip_dsi_configure_video(const struct device *dev, uint8_t channel,
					 const struct mipi_dsi_device *mdev)
{
	const struct artinchip_dsi_config *config = dev->config;
	struct artinchip_dsi_data *data = dev->data;
	uint32_t value;
	int bpp;
	int pixel_dt;

	if ((mdev->data_lanes == 0U) || (mdev->data_lanes > DSI_MAX_LANES)) {
		return -EINVAL;
	}

	bpp = artinchip_dsi_bpp(mdev->pixfmt);
	if (bpp < 0) {
		return bpp;
	}

	pixel_dt = artinchip_dsi_pixel_dt(mdev->pixfmt);
	if (pixel_dt < 0) {
		return pixel_dt;
	}

	if ((mdev->mode_flags & MIPI_DSI_MODE_VIDEO) == 0U) {
		LOG_WRN("command mode is not configured yet");
	}

	value = artinchip_dsi_read(config, DSI_DPI_VC);
	value &= ~DSI_DPI_VC_NUM_MASK;
	value |= DSI_DPI_VC_NUM(channel);
	artinchip_dsi_write(config, DSI_DPI_VC, value);

	value = artinchip_dsi_read(config, DSI_DPI_IN_FMT);
	value &= ~(DSI_DPI_IN_FMT_DT_MASK | DSI_DPI_IN_FMT_LOOSELY18);
	value |= DSI_DPI_IN_FMT_DT((uint32_t)pixel_dt);
	if (mdev->pixfmt == MIPI_DSI_PIXFMT_RGB666_PACKED) {
		value |= DSI_DPI_IN_FMT_LOOSELY18;
	}
	artinchip_dsi_write(config, DSI_DPI_IN_FMT, value);

	value = artinchip_dsi_read(config, DSI_DPI_IN_POL);
	value &= ~(DSI_DPI_IN_POL_DE | DSI_DPI_IN_POL_SHUTDOWN |
		   DSI_DPI_IN_POL_COLORM);
	value |= DSI_DPI_IN_POL_HSYNC | DSI_DPI_IN_POL_VSYNC;
	artinchip_dsi_write(config, DSI_DPI_IN_POL, value);

	value = artinchip_dsi_read(config, DSI_DPI_IF_CFG);
	value &= ~(DSI_DPI_IF_CFG_FMT_MASK | DSI_DPI_IF_CFG_SHUTD |
		   DSI_DPI_IF_CFG_COLORM);
	value |= DSI_DPI_IF_CFG_FMT(artinchip_dsi_if_cfg_fmt(mdev->pixfmt));
	artinchip_dsi_write(config, DSI_DPI_IF_CFG, value);

	value = artinchip_dsi_read(config, DSI_PHY_CFG);
	value &= ~DSI_PHY_CFG_DATA_LANE_MASK;
	value |= DSI_PHY_CFG_DATA_LANE(mdev->data_lanes - 1U);
	artinchip_dsi_write(config, DSI_PHY_CFG, value);

	artinchip_dsi_write(config, DSI_LANE_CFG, DSI_LANE_ASSIGNMENTS);
	artinchip_dsi_configure_timings(dev, mdev, (uint32_t)bpp);

	data->channel = channel;
	data->data_lanes = mdev->data_lanes;
	data->pixfmt = mdev->pixfmt;
	data->mode_flags = mdev->mode_flags;
	data->timings = mdev->timings;

	{
		uint32_t sclk_rate = 0U;

		if (clock_control_get_rate(config->clock_dev, config->clock_id,
					   &sclk_rate) == 0 &&
		    (sclk_rate != 0U)) {
			data->lane_rate = sclk_rate;
		} else {
			data->lane_rate = config->phy_clock;
		}
	}

	if (config->manual_phy_start) {
		LOG_DBG("DSI PHY start deferred");
		return 0;
	}

	return artinchip_dsi_phy_init(dev, true);
}

static int artinchip_dsi_hsclk(const struct device *dev, bool enable, bool force);

static int artinchip_dsi_reapply_video(const struct device *dev)
{
	const struct artinchip_dsi_config *config = dev->config;
	struct artinchip_dsi_data *data = dev->data;
	struct mipi_dsi_device mdev;
	uint32_t ctl;
	int ret;

	if (data->data_lanes == 0U) {
		return -ENODATA;
	}

	mdev.data_lanes = data->data_lanes;
	mdev.pixfmt = data->pixfmt;
	mdev.mode_flags = data->mode_flags;
	mdev.timings = data->timings;

	ret = artinchip_dsi_configure_video(dev, data->channel, &mdev);
	if (ret != 0) {
		return ret;
	}

	ctl = DSI_CTL_PACKET_ENABLES;
	if ((data->mode_flags & MIPI_DSI_MODE_VIDEO) == 0U) {
		ctl |= DSI_CTL_DSI_MODE;
	}
	artinchip_dsi_write(config, DSI_CTL, ctl);

	return 0;
}

int artinchip_dsi_set_videomode(const struct device *dev, bool video_enable)
{
	const struct artinchip_dsi_config *config = dev->config;
	struct artinchip_dsi_data *data = dev->data;
	int ret;

	if (data->data_lanes == 0U) {
		return -ENODATA;
	}

	if (!video_enable) {
		/* Match LubanLite: release the HS clock request before entering command
		 * mode so DCS traffic and BTA use the LP lane state machine.
		 */
		ret = artinchip_dsi_hsclk(dev, false, false);
		if (ret != 0) {
			return ret;
		}
		artinchip_dsi_write(config, DSI_CTL, DSI_CTL_PACKET_ENABLES | DSI_CTL_DSI_MODE);
		artinchip_dsi_write(config, DSI_EDPI_CMD_SIZE, data->timings.hactive);
		artinchip_dsi_update_bits(config, DSI_CMD_MODE_CFG,
					  DSI_CMD_MODE_CFG_ACK_REQ_EN |
					  DSI_CMD_MODE_CFG_TE_EN,
					  0U);
		artinchip_dsi_update_bits(config, DSI_CMD_MODE_CFG,
					  DSI_CMD_MODE_CFG_DCS_LW,
					  DSI_CMD_MODE_CFG_DCS_LW);
		return 0;
	}

	artinchip_dsi_update_bits(config, DSI_CMD_MODE_CFG, DSI_CMD_MODE_CFG_DCS_LW, 0U);

	ret = artinchip_dsi_reapply_video(dev);
	if (ret != 0) {
		return ret;
	}

	return artinchip_dsi_hsclk(dev, true, false);
}

static int artinchip_dsi_wait_phy_stop(const struct device *dev)
{
	const struct artinchip_dsi_config *config = dev->config;

	return artinchip_dsi_wait_bits_timeout(config, DSI_PHY_STA,
					       DSI_PHY_STA_STOP_MASK,
					       DSI_PHY_STA_STOP_MASK,
					       DSI_PHY_LP_TIMEOUT_US);
}

static void artinchip_dsi_lp_abort(const struct device *dev)
{
	const struct artinchip_dsi_config *config = dev->config;

	artinchip_dsi_update_bits(config, DSI_CTL, DSI_CTL_PKG_CFG_BTA_EN, 0U);
	k_busy_wait(2000);
}

static int artinchip_dsi_hsclk(const struct device *dev, bool enable, bool force)
{
	const struct artinchip_dsi_config *config = dev->config;
	uint32_t phy_cfg = artinchip_dsi_read(config, DSI_PHY_CFG);
	int ret;

	if (enable && ((phy_cfg & DSI_PHY_CFG_HSCLK_REQ) != 0U)) {
		return 0;
	}

	if (!enable) {
		if ((phy_cfg & DSI_PHY_CFG_HSCLK_REQ) == 0U) {
			return 0;
		}

		artinchip_dsi_update_bits(config, DSI_PHY_CFG, DSI_PHY_CFG_HSCLK_REQ, 0U);
		/* LubanLite waits for LP stop after releasing continuous HS clock.
		 * Treat timeout as non-fatal so command-mode entry still proceeds.
		 */
		ret = artinchip_dsi_wait_phy_stop(dev);
		if (ret != 0) {
			LOG_WRN("PHY stop timeout after HSCLK release, phy_sta=0x%08x",
				artinchip_dsi_read(config, DSI_PHY_STA));
		}
		return 0;
	}

	if (!force) {
		ret = artinchip_dsi_wait_bits_timeout(config, DSI_PHY_STA,
						      DSI_PHY_STA_STOP_STATE_C,
						      DSI_PHY_STA_STOP_STATE_C,
						      DSI_PHY_TIMEOUT_US);
		if (ret != 0) {
			return ret;
		}
	}

	artinchip_dsi_update_bits(config, DSI_PHY_CFG, DSI_PHY_CFG_HSCLK_REQ,
				  DSI_PHY_CFG_HSCLK_REQ);

	return 0;
}

int artinchip_dsi_update_timings(const struct device *dev,
				 const struct mipi_dsi_timings *timings)
{
	struct artinchip_dsi_data *data = dev->data;

	if (timings == NULL) {
		return -EINVAL;
	}

	data->timings = *timings;
	return 0;
}

enum artinchip_dsi_clock_mode {
	ARTINCHIP_DSI_CLOCK_OFF,
	ARTINCHIP_DSI_CLOCK_AUTO,
	ARTINCHIP_DSI_CLOCK_REQUEST,
};

static void artinchip_dsi_set_clock_mode(const struct device *dev,
					 enum artinchip_dsi_clock_mode mode)
{
	const struct artinchip_dsi_config *config = dev->config;
	uint32_t value = 0U;

	if (mode == ARTINCHIP_DSI_CLOCK_AUTO) {
		value = DSI_PHY_CFG_AUTO_CLK_EN;
	} else if (mode == ARTINCHIP_DSI_CLOCK_REQUEST) {
		value = DSI_PHY_CFG_HSCLK_REQ;
	}

	artinchip_dsi_update_bits(config, DSI_PHY_CFG,
				  DSI_PHY_CFG_AUTO_CLK_EN | DSI_PHY_CFG_HSCLK_REQ,
				  value);
}

static int artinchip_dsi_attach(const struct device *dev, uint8_t channel,
				const struct mipi_dsi_device *mdev)
{
	const struct artinchip_dsi_config *config = dev->config;
	struct artinchip_dsi_data *data = dev->data;
	uint32_t ctl;
	int ret;

	ret = artinchip_dsi_power_on(dev);
	if (ret != 0) {
		return ret;
	}

	ret = artinchip_dsi_configure_video(dev, channel, mdev);
	if (ret != 0) {
		return ret;
	}

	ctl = DSI_CTL_PACKET_ENABLES;
	if ((mdev->mode_flags & MIPI_DSI_MODE_VIDEO) == 0U) {
		ctl |= DSI_CTL_DSI_MODE;
	}

	artinchip_dsi_write(config, DSI_CTL, ctl);
	data->attached = true;

	return 0;
}

static ssize_t artinchip_dsi_transfer(const struct device *dev, uint8_t channel,
				      struct mipi_dsi_msg *msg)
{
	const struct artinchip_dsi_config *config = dev->config;
	struct artinchip_dsi_data *data = dev->data;
	const uint8_t *payload = msg->tx_buf;
	uint8_t d0 = msg->cmd;
	uint8_t d1 = 0U;
	uint32_t cmd_cfg = 0U;
	uint32_t header;
	size_t payload_start = 0U;
	bool need_bta;
	bool need_ack;

	if (msg == NULL) {
		return -EINVAL;
	}

	if ((msg->rx_len != 0U) && (msg->rx_buf == NULL)) {
		return -EINVAL;
	}

	if ((payload == NULL) && (msg->tx_len != 0U)) {
		return -EINVAL;
	}

	if (config->manual_phy_start && !data->phy_initialized) {
		LOG_DBG("DSI PHY not initialized; run aicdsi prepare first");
		return -EAGAIN;
	}

	switch (msg->type) {
	case MIPI_DSI_DCS_SHORT_WRITE:
		cmd_cfg = DSI_CMD_MODE_CFG_DCS_SW_0P;
		break;
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		if (msg->tx_len < 1U) {
			return -EINVAL;
		}
		cmd_cfg = DSI_CMD_MODE_CFG_DCS_SW_1P;
		d1 = payload[0];
		break;
	case MIPI_DSI_DCS_LONG_WRITE:
		cmd_cfg = DSI_CMD_MODE_CFG_DCS_LW;
		d0 = (uint8_t)((msg->tx_len + 1U) & 0xffU);
		d1 = (uint8_t)(((msg->tx_len + 1U) >> 8) & 0xffU);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		cmd_cfg = DSI_CMD_MODE_CFG_GEN_SW_0P;
		d0 = 0U;
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		if (msg->tx_len < 1U) {
			return -EINVAL;
		}
		cmd_cfg = DSI_CMD_MODE_CFG_GEN_SW_1P;
		d0 = payload[0];
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		if (msg->tx_len < 2U) {
			return -EINVAL;
		}
		cmd_cfg = DSI_CMD_MODE_CFG_GEN_SW_2P;
		d0 = payload[0];
		d1 = payload[1];
		break;
	case MIPI_DSI_GENERIC_LONG_WRITE:
		cmd_cfg = DSI_CMD_MODE_CFG_GEN_LW;
		d0 = (uint8_t)(msg->tx_len & 0xffU);
		d1 = (uint8_t)((msg->tx_len >> 8) & 0xffU);
		break;
	case MIPI_DSI_DCS_READ:
		if (msg->rx_len == 0U || msg->rx_len > sizeof(uint32_t)) {
			return -EINVAL;
		}
		cmd_cfg = DSI_CMD_MODE_CFG_DCS_SR_0P;
		break;
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		if (msg->tx_len < 2U) {
			return -EINVAL;
		}
		cmd_cfg = DSI_CMD_MODE_CFG_MAX_RD_PKG_SIZE;
		d0 = payload[0];
		d1 = payload[1];
		break;
	default:
		return -ENOTSUP;
	}

	/* Match the Luban D12P host implementation: only read transactions use
	 * BTA.  SET_MAX_RETURN_PACKET_SIZE is a write and must not leave the host
	 * waiting for an acknowledgement before the following DCS command.
	 */
	need_bta = msg->rx_len != 0U;
	need_ack = false;

	artinchip_dsi_update_bits(config, DSI_CMD_MODE_CFG,
				  DSI_CMD_MODE_CFG_SUPPORTED,
				  DSI_CMD_MODE_CFG_SUPPORTED);
	artinchip_dsi_update_bits(config, DSI_CMD_MODE_CFG,
				  DSI_CMD_MODE_CFG_ACK_REQ_EN | DSI_CMD_MODE_CFG_TE_EN,
				  need_ack ? DSI_CMD_MODE_CFG_ACK_REQ_EN : 0U);
	artinchip_dsi_update_bits(config, DSI_CMD_MODE_CFG, cmd_cfg, cmd_cfg);
	artinchip_dsi_update_bits(config, DSI_CTL, DSI_CTL_PKG_CFG_BTA_EN,
				  need_bta ? DSI_CTL_PKG_CFG_BTA_EN : 0U);

	if (msg->type == MIPI_DSI_DCS_LONG_WRITE) {
		uint32_t word = msg->cmd;
		uint32_t shift = 8U;

		for (size_t i = 0U; i < msg->tx_len; i++) {
			word |= (uint32_t)payload[i] << shift;
			shift += 8U;
			if ((shift == 32U) || (i == (msg->tx_len - 1U))) {
				if (artinchip_dsi_wait_bits_timeout(config, DSI_CMD_PKG_STA,
							    DSI_CMD_PKG_STA_PLD_W_FULL, 0U,
							    DSI_PHY_LP_TIMEOUT_US) != 0) {
					return -ETIMEDOUT;
				}
				artinchip_dsi_write(config, DSI_GEN_PD_CFG, word);
				word = 0U;
				shift = 0U;
			}
		}
	} else if (msg->type == MIPI_DSI_GENERIC_LONG_WRITE) {
		for (size_t i = payload_start; i < msg->tx_len; i += 4U) {
			uint32_t word = 0U;

			for (size_t j = 0U; (j < 4U) && ((i + j) < msg->tx_len); j++) {
				word |= (uint32_t)payload[i + j] << (j * 8U);
			}
			if (artinchip_dsi_wait_bits_timeout(config, DSI_CMD_PKG_STA,
						    DSI_CMD_PKG_STA_PLD_W_FULL, 0U,
						    DSI_PHY_LP_TIMEOUT_US) != 0) {
				return -ETIMEDOUT;
			}
			artinchip_dsi_write(config, DSI_GEN_PD_CFG, word);
		}
	}

	/* Match LubanLite: wait for clock + data lane 0 LP stop before the header. */
	if (artinchip_dsi_wait_phy_stop(dev) != 0) {
		LOG_ERR("PHY did not enter LP stop state, phy_sta=0x%08x ctl=0x%08x",
			artinchip_dsi_read(config, DSI_PHY_STA),
			artinchip_dsi_read(config, DSI_CTL));
		return -ETIMEDOUT;
	}

	header = (uint32_t)msg->type | ((uint32_t)channel << 6) |
		 ((uint32_t)d0 << 8) | ((uint32_t)d1 << 16);
	if (artinchip_dsi_wait_bits_timeout(config, DSI_CMD_PKG_STA,
					    DSI_CMD_PKG_STA_CMD_FULL, 0U,
					    DSI_PHY_LP_TIMEOUT_US) != 0) {
		LOG_ERR("DSI command FIFO remained full, cmd=0x%02x cmd_sta=0x%08x "
			"phy_sta=0x%08x ctl=0x%08x cmd_cfg=0x%08x",
			msg->cmd, artinchip_dsi_read(config, DSI_CMD_PKG_STA),
			artinchip_dsi_read(config, DSI_PHY_STA),
			artinchip_dsi_read(config, DSI_CTL),
			artinchip_dsi_read(config, DSI_CMD_MODE_CFG));
		return -ETIMEDOUT;
	}
	artinchip_dsi_write(config, DSI_GEN_PH_CFG, header);

	if (msg->rx_len != 0U) {
		uint32_t response;

		/* Wait for the BTA read transaction to complete before consuming the
		 * response. Reading GEN_PD_CFG while RD_CMD_BUSY is still asserted
		 * returns stale zero and leaves later long writes blocked.
		 */
		k_busy_wait(10);
		if (artinchip_dsi_wait_bits_timeout(config, DSI_CMD_PKG_STA,
						    DSI_CMD_PKG_STA_RD_CMD_BUSY, 0U,
						    DSI_PHY_LP_TIMEOUT_US) != 0) {
			LOG_ERR("DSI read transaction timeout, cmd=0x%02x cmd_sta=0x%08x",
				msg->cmd, artinchip_dsi_read(config, DSI_CMD_PKG_STA));
			artinchip_dsi_lp_abort(dev);
			return -ETIMEDOUT;
		}
		response = artinchip_dsi_read(config, DSI_GEN_PD_CFG);
		memcpy(msg->rx_buf, &response, msg->rx_len);

		/* Release bus ownership before waiting for LP stop state. Waiting while
		 * BTA_EN remains asserted keeps lane 0 in receive direction, so later
		 * command headers accumulate without ever reaching the panel.
		 */
		artinchip_dsi_update_bits(config, DSI_CTL, DSI_CTL_PKG_CFG_BTA_EN, 0U);
		if (artinchip_dsi_wait_phy_stop(dev) != 0) {
			LOG_ERR("DSI did not leave BTA receive state, phy_sta=0x%08x cmd_sta=0x%08x",
				artinchip_dsi_read(config, DSI_PHY_STA),
				artinchip_dsi_read(config, DSI_CMD_PKG_STA));
			artinchip_dsi_lp_abort(dev);
			return -ETIMEDOUT;
		}
		k_busy_wait(DSI_CMD_GUARD_TIME_US);
		return msg->rx_len;
	}

	/* D125 does not reliably assert CMD_EMPTY between LP commands (0x14 is a
	 * normal observed state). Luban waits for the payload write FIFO to drain,
	 * then delays before submitting the next header. Keep the pre-submit
	 * CMD_FULL check above and use a larger guard for delay-free panel tables.
	 */
	if (artinchip_dsi_wait_bits_timeout(config, DSI_CMD_PKG_STA,
					    DSI_CMD_PKG_STA_PLD_W_EMPTY,
					    DSI_CMD_PKG_STA_PLD_W_EMPTY,
					    DSI_PHY_LP_TIMEOUT_US) != 0) {
		LOG_ERR("DSI packet completion timeout, cmd=0x%02x cmd_sta=0x%08x",
			msg->cmd, artinchip_dsi_read(config, DSI_CMD_PKG_STA));
		return -ETIMEDOUT;
	}
	k_busy_wait(DSI_CMD_GUARD_TIME_US);
	if (need_bta) {
		if (artinchip_dsi_wait_phy_stop(dev) != 0) {
			artinchip_dsi_lp_abort(dev);
			return -ETIMEDOUT;
		}
		artinchip_dsi_update_bits(config, DSI_CTL, DSI_CTL_PKG_CFG_BTA_EN, 0U);
	}

	return msg->tx_len + ((msg->type == MIPI_DSI_DCS_LONG_WRITE) ? 1U : 0U);
}

static int artinchip_dsi_detach(const struct device *dev, uint8_t channel,
				const struct mipi_dsi_device *mdev)
{
	ARG_UNUSED(channel);
	ARG_UNUSED(mdev);

	return artinchip_dsi_power_off(dev);
}

static int artinchip_dsi_init(const struct device *dev)
{
	const struct artinchip_dsi_config *config = dev->config;
	int ret;

	if (!device_is_ready(config->clock_dev)) {
		return -ENODEV;
	}

	if (!device_is_ready(config->reset.dev)) {
		return -ENODEV;
	}

	ret = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
	return (ret == -ENOENT) ? 0 : ret;
}

static DEVICE_API(mipi_dsi, artinchip_dsi_api) = {
	.attach = artinchip_dsi_attach,
	.transfer = artinchip_dsi_transfer,
	.detach = artinchip_dsi_detach,
};

int artinchip_dsi_prepare(const struct device *dev)
{
	const struct artinchip_dsi_config *config = dev->config;
	int ret;

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	ret = artinchip_dsi_power_on(dev);
	if (ret != 0) {
		LOG_ERR("power on failed: %d", ret);
		return ret;
	}

	if (!((struct artinchip_dsi_data *)dev->data)->phy_initialized) {
		ret = artinchip_dsi_phy_init(dev, true);
		if (ret != 0) {
			LOG_ERR("PHY init failed: %d", ret);
			return ret;
		}
	}

	artinchip_dsi_write(config, DSI_IRQ_STA1, 0xffffffffU);
	artinchip_dsi_write(config, DSI_IRQ_STA2, 0xffffffffU);
	artinchip_dsi_set_clock_mode(dev, ARTINCHIP_DSI_CLOCK_REQUEST);

	ret = artinchip_dsi_hsclk(dev, true, true);
	if (ret != 0) {
		LOG_ERR("HS clock request failed: %d phy_sta=0x%08x phy_cfg=0x%08x", ret,
			artinchip_dsi_read(config, DSI_PHY_STA),
			artinchip_dsi_read(config, DSI_PHY_CFG));
		return ret;
	}

	return 0;
}

#if defined(CONFIG_MIPI_DSI_ARTINCHIP_SHELL)
static const struct device *const artinchip_dsi_shell_dev =
	DEVICE_DT_GET(DT_DRV_INST(0));

static int artinchip_dsi_shell_status(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_dsi_shell_dev;
	const struct artinchip_dsi_config *config = dev->config;
	const struct artinchip_dsi_data *data = dev->data;
	uint32_t clock_rate;
	uint32_t pixclk_rate;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!device_is_ready(dev)) {
		shell_error(sh, "MIPI DSI device is not ready");
		return -ENODEV;
	}

	shell_print(sh, "powered: %s", data->powered ? "yes" : "no");
	shell_print(sh, "manual-phy-start: %s phy-initialized: %s",
		    config->manual_phy_start ? "yes" : "no",
		    data->phy_initialized ? "yes" : "no");
	shell_print(sh, "attached: channel=%u lanes=%u pixfmt=%u flags=0x%08x",
		    data->channel, data->data_lanes, data->pixfmt, data->mode_flags);
	shell_print(sh, "phy-clock: %u Hz lane-rate-estimate: %u", config->phy_clock,
		    data->lane_rate);
	ret = clock_control_get_rate(config->clock_dev, config->clock_id, &clock_rate);
	if (ret == 0) {
		shell_print(sh, "sclk-rate: %u Hz", clock_rate);
	} else {
		shell_print(sh, "sclk-rate: unavailable (%d)", ret);
	}

	ret = clock_control_get_rate(config->clock_dev,
				     (clock_control_subsys_t)ARTINCHIP_CLK_RGB,
				     &pixclk_rate);
	if (ret == 0) {
		shell_print(sh, "pixclk-rate: %u Hz", pixclk_rate);
	} else {
		shell_print(sh, "pixclk-rate: unavailable (%d)", ret);
	}
#if DT_HAS_COMPAT_STATUS_OKAY(artinchip_d12p_cctl)
	shell_print(sh, "cmu: pll_gen=0x%08x pll_cfg=0x%08x pll_sdm=0x%08x",
		    sys_read32(AIC_DSI_CMU_BASE + AIC_DSI_PLL_FRA2_GEN_REG),
		    sys_read32(AIC_DSI_CMU_BASE + AIC_DSI_PLL_FRA2_CFG_REG),
		    sys_read32(AIC_DSI_CMU_BASE + AIC_DSI_PLL_FRA2_SDM_REG));
	shell_print(sh, "cmu: disp_sclk=0x%08x disp_pix=0x%08x",
		    sys_read32(AIC_DSI_CMU_BASE + AIC_DSI_DISP_SCLK_REG),
		    sys_read32(AIC_DSI_CMU_BASE + AIC_DSI_DISP_PIXCLK_REG));
#endif
	shell_print(sh, "ctl=0x%08x clk_cfg=0x%08x phy_cfg=0x%08x phy_sta=0x%08x",
		    artinchip_dsi_read(config, DSI_CTL),
		    artinchip_dsi_read(config, DSI_CLK_CFG),
		    artinchip_dsi_read(config, DSI_PHY_CFG),
		    artinchip_dsi_read(config, DSI_PHY_STA));
	shell_print(sh, "dpi_pol=0x%08x dpi_fmt=0x%08x dpi_vc=0x%08x dpi_if=0x%08x",
		    artinchip_dsi_read(config, DSI_DPI_IN_POL),
		    artinchip_dsi_read(config, DSI_DPI_IN_FMT),
		    artinchip_dsi_read(config, DSI_DPI_VC),
		    artinchip_dsi_read(config, DSI_DPI_IF_CFG));
	shell_print(sh, "vid_mode=0x%08x pkg=0x%08x hinact=0x%08x ht=0x%08x",
		    artinchip_dsi_read(config, DSI_VID_MODE_CFG),
		    artinchip_dsi_read(config, DSI_VID_PKG_SIZE),
		    artinchip_dsi_read(config, DSI_VID_HINACT_TIME),
		    artinchip_dsi_read(config, DSI_VID_HT_TIME));
	shell_print(sh, "vblank=0x%08x vact=0x%08x cmd_cfg=0x%08x cmd_sta=0x%08x",
		    artinchip_dsi_read(config, DSI_VID_VBLANK_LINE),
		    artinchip_dsi_read(config, DSI_VID_VACT_LINE),
		    artinchip_dsi_read(config, DSI_CMD_MODE_CFG),
		    artinchip_dsi_read(config, DSI_CMD_PKG_STA));
	shell_print(sh, "lane_cfg=0x%08x phy_test1=0x%08x phy_test2=0x%08x",
		    artinchip_dsi_read(config, DSI_LANE_CFG),
		    artinchip_dsi_read(config, DSI_PHY_TEST1),
		    artinchip_dsi_read(config, DSI_PHY_TEST2));
	shell_print(sh, "ana_cfg1=0x%08x ana_cfg2=0x%08x",
		    artinchip_dsi_read(config, DSI_ANA_CFG1),
		    artinchip_dsi_read(config, DSI_ANA_CFG2));
	uint32_t irq_sta2 = artinchip_dsi_read(config, DSI_IRQ_STA2);

	shell_print(sh, "irq_sta1=0x%08x irq_sta2=0x%08x%s",
		    artinchip_dsi_read(config, DSI_IRQ_STA1), irq_sta2,
		    (irq_sta2 & DSI_IRQ_STA2_DPI_FIFO_OV) != 0U ? " DPI_FIFO_OV" : "");

	return 0;
}

static int artinchip_dsi_shell_phy_init(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_dsi_shell_dev;
	bool analog = false;
	int ret;

	if (argc > 2) {
		shell_error(sh, "usage: aicdsi phy-init [analog]");
		return -EINVAL;
	}

	if ((argc == 2) && (strcmp(argv[1], "analog") == 0)) {
		analog = true;
	} else if (argc == 2) {
		shell_error(sh, "usage: aicdsi phy-init [analog]");
		return -EINVAL;
	}

	if (analog) {
		(void)artinchip_dsi_power_off(dev);
		k_busy_wait(100);
	}

	ret = artinchip_dsi_power_on(dev);
	if (ret != 0) {
		shell_error(sh, "power on failed: %d", ret);
		return ret;
	}

	ret = artinchip_dsi_phy_init(dev, analog);
	if (ret != 0) {
		shell_error(sh, "PHY init failed: %d", ret);
		return ret;
	}

	shell_print(sh, "MIPI DSI PHY initialized%s", analog ? " with analog setup" : "");

	ret = artinchip_dsi_reapply_video(dev);
	if (ret == 0) {
		shell_print(sh, "MIPI DSI video configuration reapplied");
	} else if (ret != -ENODATA) {
		shell_error(sh, "video reapply failed: %d", ret);
		return ret;
	}

	return 0;
}

static int artinchip_dsi_shell_video_start(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = artinchip_dsi_reapply_video(artinchip_dsi_shell_dev);
	if (ret != 0) {
		shell_error(sh, "video start failed: %d", ret);
		return ret;
	}

	shell_print(sh, "MIPI DSI video configuration started");
	return 0;
}

static int artinchip_dsi_shell_prepare(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = artinchip_dsi_prepare(artinchip_dsi_shell_dev);
	if (ret != 0) {
		shell_error(sh, "host prepare failed: %d", ret);
	}

	return ret;
}

static int artinchip_dsi_shell_pulse_test(const struct shell *sh, size_t argc, char **argv)
{
	struct mipi_dsi_msg msg = {
		.type = MIPI_DSI_DCS_SHORT_WRITE,
		.cmd = 0x00,
	};
	int count = 1;
	ssize_t ret;

	if (argc > 2) {
		shell_error(sh, "usage: aicdsi pulse-test [count]");
		return -EINVAL;
	}

	if (argc == 2) {
		count = (int)strtol(argv[1], NULL, 0);
		if (count <= 0) {
			shell_error(sh, "count must be positive");
			return -EINVAL;
		}
	}

	for (int i = 0; i < count; i++) {
		ret = artinchip_dsi_transfer(artinchip_dsi_shell_dev, 0, &msg);
		if (ret < 0) {
			shell_error(sh, "pulse packet %d failed: %d", i, (int)ret);
			return (int)ret;
		}
	}

	shell_print(sh, "sent %d DCS NOP packet(s)", count);
	return 0;
}

static int artinchip_dsi_shell_power(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_dsi_shell_dev;
	int ret;

	if (argc != 2) {
		shell_error(sh, "usage: aicdsi power <on|off>");
		return -EINVAL;
	}

	if (strcmp(argv[1], "on") == 0) {
		ret = artinchip_dsi_power_on(dev);
	} else if (strcmp(argv[1], "off") == 0) {
		ret = artinchip_dsi_power_off(dev);
	} else {
		shell_error(sh, "usage: aicdsi power <on|off>");
		return -EINVAL;
	}

	if (ret != 0) {
		shell_error(sh, "power %s failed: %d", argv[1], ret);
		return ret;
	}

	shell_print(sh, "MIPI DSI power %s", argv[1]);
	return 0;
}

static int artinchip_dsi_shell_hsclk(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_dsi_shell_dev;
	const struct artinchip_dsi_config *config = dev->config;
	bool enable;
	bool force = false;
	int ret;

	if (argc != 2) {
		shell_error(sh, "usage: aicdsi hsclk <on|off|force-on>");
		return -EINVAL;
	}

	if (strcmp(argv[1], "on") == 0) {
		enable = true;
	} else if (strcmp(argv[1], "force-on") == 0) {
		enable = true;
		force = true;
	} else if (strcmp(argv[1], "off") == 0) {
		enable = false;
	} else {
		shell_error(sh, "usage: aicdsi hsclk <on|off|force-on>");
		return -EINVAL;
	}

	ret = artinchip_dsi_hsclk(dev, enable, force);
	if (ret != 0) {
		shell_error(sh, "HS clock %s failed: %d phy_sta=0x%08x phy_cfg=0x%08x",
			    argv[1], ret, artinchip_dsi_read(config, DSI_PHY_STA),
			    artinchip_dsi_read(config, DSI_PHY_CFG));
		return ret;
	}

	shell_print(sh, "MIPI DSI HS clock %s", argv[1]);
	return 0;
}

static int artinchip_dsi_shell_clock_mode(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev = artinchip_dsi_shell_dev;
	const struct artinchip_dsi_config *config = dev->config;
	enum artinchip_dsi_clock_mode mode;

	if (argc != 2) {
		shell_error(sh, "usage: aicdsi clock-mode <auto|request|off>");
		return -EINVAL;
	}

	if (strcmp(argv[1], "auto") == 0) {
		mode = ARTINCHIP_DSI_CLOCK_AUTO;
	} else if (strcmp(argv[1], "request") == 0) {
		mode = ARTINCHIP_DSI_CLOCK_REQUEST;
	} else if (strcmp(argv[1], "off") == 0) {
		mode = ARTINCHIP_DSI_CLOCK_OFF;
	} else {
		shell_error(sh, "usage: aicdsi clock-mode <auto|request|off>");
		return -EINVAL;
	}

	artinchip_dsi_set_clock_mode(dev, mode);
	shell_print(sh, "MIPI DSI clock mode %s phy_cfg=0x%08x phy_sta=0x%08x",
		    argv[1], artinchip_dsi_read(config, DSI_PHY_CFG),
		    artinchip_dsi_read(config, DSI_PHY_STA));

	return 0;
}

static int artinchip_dsi_shell_irq_clear(const struct shell *sh, size_t argc, char **argv)
{
	const struct artinchip_dsi_config *config = artinchip_dsi_shell_dev->config;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	artinchip_dsi_write(config, DSI_IRQ_STA1, 0xffffffffU);
	artinchip_dsi_write(config, DSI_IRQ_STA2, 0xffffffffU);
	shell_print(sh, "MIPI DSI IRQ status cleared");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_aicdsi,
	SHELL_CMD_ARG(clock-mode, NULL, "Control DSI clock lane mode: clock-mode <auto|request|off>",
		      artinchip_dsi_shell_clock_mode, 2, 0),
	SHELL_CMD(prepare, NULL, "Power and prepare the DSI host/PHY",
		  artinchip_dsi_shell_prepare),
	SHELL_CMD_ARG(hsclk, NULL, "Control DSI HS clock request: hsclk <on|off|force-on>",
		      artinchip_dsi_shell_hsclk, 2, 0),
	SHELL_CMD(irq-clear, NULL, "Clear MIPI DSI IRQ status", artinchip_dsi_shell_irq_clear),
	SHELL_CMD_ARG(phy-init, NULL, "Initialize DSI PHY: phy-init [analog]",
		      artinchip_dsi_shell_phy_init, 1, 1),
	SHELL_CMD_ARG(power, NULL, "Control MIPI DSI clock/reset: power <on|off>",
		      artinchip_dsi_shell_power, 2, 0),
	SHELL_CMD_ARG(pulse-test, NULL, "Send DCS NOP packet(s): pulse-test [count]",
		      artinchip_dsi_shell_pulse_test, 1, 1),
	SHELL_CMD(status, NULL, "Show MIPI DSI registers", artinchip_dsi_shell_status),
	SHELL_CMD(video-start, NULL, "Reapply attached DSI video configuration",
		  artinchip_dsi_shell_video_start),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(aicdsi, &sub_aicdsi, "ArtInChip MIPI DSI diagnostics", NULL);
#endif

#define ARTINCHIP_DSI_INIT(inst)								\
	PINCTRL_DT_INST_DEFINE(inst);							\
	static struct artinchip_dsi_data artinchip_dsi_data_##inst;				\
	static const struct artinchip_dsi_config artinchip_dsi_config_##inst = {		\
		.base = DT_INST_REG_ADDR(inst),							\
		.phy_clock = DT_INST_PROP(inst, phy_clock),					\
		.manual_phy_start = DT_INST_PROP(inst, artinchip_manual_phy_start),		\
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),				\
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),			\
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid),	\
		.reset = RESET_DT_SPEC_INST_GET(inst),					\
	};											\
	DEVICE_DT_INST_DEFINE(inst, artinchip_dsi_init, NULL,				\
			      &artinchip_dsi_data_##inst, &artinchip_dsi_config_##inst,	\
			      POST_KERNEL, CONFIG_MIPI_DSI_INIT_PRIORITY,			\
			      &artinchip_dsi_api);

DT_INST_FOREACH_STATUS_OKAY(ARTINCHIP_DSI_INIT)
