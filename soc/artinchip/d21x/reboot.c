/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define D21X_CMU_NODE DT_NODELABEL(cmu)
#define D21X_RTC_NODE DT_NODELABEL(rtc)
#define D21X_WDT_NODE DT_NODELABEL(wdt)

#define D21X_CMU_WDT_CLK_REG       0x020CU
#define D21X_CMU_WDT_CLK_BUS_GATE  BIT(12)
#define D21X_CMU_WDT_CLK_MOD_GATE  BIT(8)
#define D21X_CMU_WDT_RESET_N       BIT(13)

#define D21X_WDT_CTL               0x000U
#define D21X_WDT_IRQ_EN            0x008U
#define D21X_WDT_IRQ_STA           0x00CU
#define D21X_WDT_CLR_THD           0x040U
#define D21X_WDT_IRQ_THD           0x044U
#define D21X_WDT_RST_THD           0x048U
#define D21X_WDT_OP                0x0E8U
#define D21X_WDT_CTL_DBG_CONTINUE  BIT(1)
#define D21X_WDT_CTL_ENABLE        BIT(0)
#define D21X_WDT_OP_WR_EN_CMD0     0xA1C99999U
#define D21X_WDT_OP_WR_EN_CMD1     0xA1C66666U
#define D21X_WDT_OP_CNT_CLR_CMD0   0xA1C55555U
#define D21X_WDT_OP_CNT_CLR_CMD1   0xA1CAAAAAU
#define D21X_WDT_OP_CFG_SW_CMD0(n) (0xA1C5A5A0U | (n))
#define D21X_WDT_OP_CFG_SW_CMD1(n) (0xA1CA5A50U | (n))
#define D21X_WDT_SEC_TO_CNT(n)     ((n) * 32000U)

/* D21x WRI v1.0 stores the reboot reason in the RTC backup byte. */
#define D21X_RTC_REG_WR_EN         0x0FCU
#define D21X_RTC_WR_EN_KEY         0xACU
#define D21X_RTC_BOOT_INFO         0x100U
#define D21X_RTC_REBOOT_REASON     GENMASK(7, 4)

void artinchip_soc_set_reboot_reason(uint32_t reason)
{
	uintptr_t base = DT_REG_ADDR(D21X_RTC_NODE);
	uint8_t boot_info = sys_read8(base + D21X_RTC_BOOT_INFO);

	boot_info &= (uint8_t)~D21X_RTC_REBOOT_REASON;
	boot_info |= (uint8_t)FIELD_PREP(D21X_RTC_REBOOT_REASON, reason);

	sys_write8(D21X_RTC_WR_EN_KEY, base + D21X_RTC_REG_WR_EN);
	sys_write8(boot_info, base + D21X_RTC_BOOT_INFO);
	sys_write8(0U, base + D21X_RTC_REG_WR_EN);
}

static void artinchip_d21x_watchdog_reset(void)
{
	uintptr_t cmu = DT_REG_ADDR(D21X_CMU_NODE);
	uintptr_t wdt = DT_REG_ADDR(D21X_WDT_NODE);
	uint32_t clk;

	clk = sys_read32(cmu + D21X_CMU_WDT_CLK_REG);
	clk |= D21X_CMU_WDT_CLK_BUS_GATE | D21X_CMU_WDT_CLK_MOD_GATE;
	sys_write32(clk, cmu + D21X_CMU_WDT_CLK_REG);

	clk &= ~D21X_CMU_WDT_RESET_N;
	sys_write32(clk, cmu + D21X_CMU_WDT_CLK_REG);
	k_busy_wait(30);
	clk |= D21X_CMU_WDT_RESET_N;
	sys_write32(clk, cmu + D21X_CMU_WDT_CLK_REG);
	k_busy_wait(30);

	sys_write32(D21X_WDT_OP_WR_EN_CMD0, wdt + D21X_WDT_OP);
	sys_write32(D21X_WDT_OP_WR_EN_CMD1, wdt + D21X_WDT_OP);
	sys_write32(0U, wdt + D21X_WDT_IRQ_EN);
	sys_write32(0U, wdt + D21X_WDT_CTL);
	sys_write32(0xffffffffU, wdt + D21X_WDT_IRQ_STA);
	sys_write32(0U, wdt + D21X_WDT_CLR_THD);
	sys_write32(0U, wdt + D21X_WDT_IRQ_THD);
	sys_write32(D21X_WDT_SEC_TO_CNT(1), wdt + D21X_WDT_RST_THD);
	sys_write32(D21X_WDT_OP_CFG_SW_CMD0(0), wdt + D21X_WDT_OP);
	sys_write32(D21X_WDT_OP_CFG_SW_CMD1(0), wdt + D21X_WDT_OP);
	sys_write32(D21X_WDT_OP_CNT_CLR_CMD0, wdt + D21X_WDT_OP);
	sys_write32(D21X_WDT_OP_CNT_CLR_CMD1, wdt + D21X_WDT_OP);
	sys_write32(D21X_WDT_CTL_DBG_CONTINUE | D21X_WDT_CTL_ENABLE, wdt + D21X_WDT_CTL);
}

void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	artinchip_d21x_watchdog_reset();

	for (;;) {
		k_cpu_idle();
	}
}
