/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define AIC_WRI_NODE DT_NODELABEL(wri)
#define AIC_WRI_RST_FLAG 0x000U

#define AIC_WRI_FLAG_POR  BIT(0)
#define AIC_WRI_FLAG_RTC  BIT(1)
#define AIC_WRI_FLAG_EXT  BIT(8)
#define AIC_WRI_FLAG_DM   BIT(9)
#define AIC_WRI_FLAG_WDT  BIT(10)
#define AIC_WRI_FLAG_TSEN BIT(11)
#define AIC_WRI_FLAG_CMP  BIT(12)
#define AIC_WRI_FLAG_MASK (GENMASK(12, 8) | AIC_WRI_FLAG_RTC | AIC_WRI_FLAG_POR)

static const uintptr_t artinchip_wri_base = DT_REG_ADDR(AIC_WRI_NODE);

int z_impl_hwinfo_get_reset_cause(uint32_t *cause)
{
	uint32_t flags = sys_read32(artinchip_wri_base + AIC_WRI_RST_FLAG);
	uint32_t result = 0U;

	if ((flags & AIC_WRI_FLAG_POR) != 0U) {
		result |= RESET_POR;
	}
	if ((flags & AIC_WRI_FLAG_RTC) != 0U) {
		result |= RESET_LOW_POWER_WAKE;
	}
	if ((flags & AIC_WRI_FLAG_EXT) != 0U) {
		result |= RESET_PIN;
	}
	if ((flags & AIC_WRI_FLAG_DM) != 0U) {
		result |= RESET_DEBUG;
	}
	if ((flags & AIC_WRI_FLAG_WDT) != 0U) {
		result |= RESET_WATCHDOG;
	}
	if ((flags & AIC_WRI_FLAG_TSEN) != 0U) {
		result |= RESET_TEMPERATURE;
	}
	if ((flags & AIC_WRI_FLAG_CMP) != 0U) {
		result |= RESET_BROWNOUT;
	}

	*cause = result;

	return 0;
}

int z_impl_hwinfo_clear_reset_cause(void)
{
	uint32_t flags = sys_read32(artinchip_wri_base + AIC_WRI_RST_FLAG);

	sys_write32(flags & AIC_WRI_FLAG_MASK, artinchip_wri_base + AIC_WRI_RST_FLAG);

	return 0;
}

int z_impl_hwinfo_get_supported_reset_cause(uint32_t *supported)
{
	*supported = RESET_POR | RESET_LOW_POWER_WAKE | RESET_PIN | RESET_DEBUG | RESET_WATCHDOG |
		     RESET_TEMPERATURE | RESET_BROWNOUT;

	return 0;
}

#if DT_HAS_COMPAT_STATUS_OKAY(artinchip_d12p_sid)

/*
 * Device ID is the factory-programmed chip unique ID stored in the SID/eFuse
 * block. Register map, read handshake and timing value are derived from the
 * luban-lite hal_efuse reference: each logical word is stored redundantly in
 * two banks (word index wid and wid + AIC_SID_MAX_WORD) whose reads are OR'd
 * together, and the chip ID occupies 16 bytes starting at byte address 0x10.
 */
#define AIC_SID_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(artinchip_d12p_sid)

#define AIC_SID_CTL    0x000U
#define AIC_SID_ADDR   0x004U
#define AIC_SID_RDATA  0x00CU
#define AIC_SID_TIMING 0x010U

#define AIC_SID_TIMING_VALUE 0x04021FF1U
#define AIC_SID_MAX_WORD     64U
#define AIC_SID_OP_CODE      0xA1CU
#define AIC_SID_CTL_READ     BIT(4)
#define AIC_SID_STS_MASK     GENMASK(11, 8)
#define AIC_SID_STS_IDLE     (2U << 8)
#define AIC_SID_CHIPID_WORD  4U  /* byte address 0x10 >> 2 */
#define AIC_SID_CHIPID_LEN   16U

static const uintptr_t aic_sid_base = DT_REG_ADDR(AIC_SID_NODE);
static const struct device *const aic_sid_clk =
	DEVICE_DT_GET(DT_CLOCKS_CTLR(AIC_SID_NODE));
static const clock_control_subsys_t aic_sid_clkid =
	(clock_control_subsys_t)DT_CLOCKS_CELL(AIC_SID_NODE, clkid);
static const struct reset_dt_spec aic_sid_reset = RESET_DT_SPEC_GET(AIC_SID_NODE);

static int aic_sid_wait_ready(void)
{
	for (int i = 1000; i > 0; i--) {
		if ((sys_read32(aic_sid_base + AIC_SID_CTL) & AIC_SID_STS_MASK) ==
		    AIC_SID_STS_IDLE) {
			return 0;
		}
	}

	return -EIO;
}

static uint32_t aic_sid_read_word(uint32_t wid)
{
	uint32_t rval = 0;

	for (uint32_t i = 0; i < 2U; i++) {
		uint32_t addr = (wid + AIC_SID_MAX_WORD * i) << 2;
		uint32_t val;

		sys_write32(addr, aic_sid_base + AIC_SID_ADDR);

		val = sys_read32(aic_sid_base + AIC_SID_CTL);
		val &= ~((0xFFFU << 16) | AIC_SID_CTL_READ);
		val |= (AIC_SID_OP_CODE << 16) | AIC_SID_CTL_READ;
		sys_write32(val, aic_sid_base + AIC_SID_CTL);

		while (sys_read32(aic_sid_base + AIC_SID_CTL) & AIC_SID_CTL_READ) {
		}

		rval |= sys_read32(aic_sid_base + AIC_SID_RDATA);
	}

	return rval;
}

ssize_t z_impl_hwinfo_get_device_id(uint8_t *buffer, size_t length)
{
	uint8_t id[AIC_SID_CHIPID_LEN];
	int ret;

	if (!device_is_ready(aic_sid_clk)) {
		return -ENODEV;
	}

	ret = clock_control_on(aic_sid_clk, aic_sid_clkid);
	if (ret < 0) {
		return ret;
	}

	(void)reset_line_deassert_dt(&aic_sid_reset);
	sys_write32(AIC_SID_TIMING_VALUE, aic_sid_base + AIC_SID_TIMING);

	ret = aic_sid_wait_ready();
	if (ret < 0) {
		(void)clock_control_off(aic_sid_clk, aic_sid_clkid);
		return ret;
	}

	for (uint32_t w = 0; w < AIC_SID_CHIPID_LEN / 4U; w++) {
		sys_put_le32(aic_sid_read_word(AIC_SID_CHIPID_WORD + w), &id[w * 4U]);
	}

	(void)clock_control_off(aic_sid_clk, aic_sid_clkid);

	length = MIN(length, sizeof(id));
	memcpy(buffer, id, length);

	return (ssize_t)length;
}

#endif /* DT_HAS_COMPAT_STATUS_OKAY(artinchip_d12p_sid) */
