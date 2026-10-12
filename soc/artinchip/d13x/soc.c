/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * D13x clock, reset, pinctrl, cache, and timer initialization are intentionally
 * deferred until their bootloader ownership and hardware behavior are verified.
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define D13X_CMU_NODE DT_NODELABEL(cmu)
#define D13X_RTC_NODE DT_NODELABEL(rtc)
#define D13X_WDT_NODE DT_NODELABEL(wdt)

#define D13X_CMU_WDT_CLK_REG       0x020CU
#define D13X_CMU_WDT_CLK_BUS_GATE  BIT(12)
#define D13X_CMU_WDT_CLK_MOD_GATE  BIT(8)
#define D13X_CMU_WDT_RESET_N       BIT(13)

#define D13X_WDT_CTL               0x000U
#define D13X_WDT_IRQ_EN            0x008U
#define D13X_WDT_IRQ_STA           0x00CU
#define D13X_WDT_RST_SEL           0x020U
#define D13X_WDT_IRQ_THD           0x044U
#define D13X_WDT_RST_THD           0x048U
#define D13X_WDT_OP                0x0E8U
#define D13X_WDT_CTL_DBG_CONTINUE  BIT(1)
#define D13X_WDT_CTL_ENABLE        BIT(0)
#define D13X_WDT_OP_WR_EN_CMD0     0xA1C99999U
#define D13X_WDT_OP_WR_EN_CMD1     0xA1C66666U
#define D13X_WDT_OP_CNT_CLR_CMD0   0xA1C55555U
#define D13X_WDT_OP_CNT_CLR_CMD1   0xA1CAAAAAU
#define D13X_WDT_OP_CFG_SW_CMD0(n) (0xA1C5A5A0U | (n))
#define D13X_WDT_OP_CFG_SW_CMD1(n) (0xA1CA5A50U | (n))
#define D13X_WDT_SEC_TO_CNT(n)     ((n) * 32000U)
#define D13X_WDT_RESET_SOC         0U

#define D13X_RTC_REG_WR_EN         0x0FCU
#define D13X_RTC_WR_EN_KEY         0xACU
#define D13X_RTC_BOOT_INFO         0x100U
#define D13X_RTC_REBOOT_REASON     GENMASK(7, 4)

#if DT_NODE_EXISTS(D13X_RTC_NODE)
void artinchip_soc_set_reboot_reason(uint32_t reason)
{
	uintptr_t base = DT_REG_ADDR(D13X_RTC_NODE);
	uint8_t boot_info = sys_read8(base + D13X_RTC_BOOT_INFO);

	boot_info &= (uint8_t)~D13X_RTC_REBOOT_REASON;
	boot_info |= (uint8_t)FIELD_PREP(D13X_RTC_REBOOT_REASON, reason);

	sys_write8(D13X_RTC_WR_EN_KEY, base + D13X_RTC_REG_WR_EN);
	sys_write8(boot_info, base + D13X_RTC_BOOT_INFO);
	sys_write8(0U, base + D13X_RTC_REG_WR_EN);
}
#endif

#if DT_NODE_HAS_STATUS(D13X_CMU_NODE, okay) && DT_NODE_EXISTS(D13X_WDT_NODE)
static void artinchip_d13x_watchdog_reset(void)
{
	uintptr_t cmu = DT_REG_ADDR(D13X_CMU_NODE);
	uintptr_t wdt = DT_REG_ADDR(D13X_WDT_NODE);
	uint32_t clk;

	clk = sys_read32(cmu + D13X_CMU_WDT_CLK_REG);
	clk |= D13X_CMU_WDT_CLK_BUS_GATE | D13X_CMU_WDT_CLK_MOD_GATE;
	sys_write32(clk, cmu + D13X_CMU_WDT_CLK_REG);

	clk &= ~D13X_CMU_WDT_RESET_N;
	sys_write32(clk, cmu + D13X_CMU_WDT_CLK_REG);
	k_busy_wait(30);
	clk |= D13X_CMU_WDT_RESET_N;
	sys_write32(clk, cmu + D13X_CMU_WDT_CLK_REG);
	k_busy_wait(30);

	sys_write32(D13X_WDT_OP_WR_EN_CMD0, wdt + D13X_WDT_OP);
	sys_write32(D13X_WDT_OP_WR_EN_CMD1, wdt + D13X_WDT_OP);
	sys_write32(0U, wdt + D13X_WDT_IRQ_EN);
	sys_write32(0U, wdt + D13X_WDT_CTL);
	sys_write32(0xffffffffU, wdt + D13X_WDT_IRQ_STA);
	sys_write32(0U, wdt + D13X_WDT_IRQ_THD);
	sys_write32(D13X_WDT_SEC_TO_CNT(1), wdt + D13X_WDT_RST_THD);
	sys_write32(D13X_WDT_RESET_SOC, wdt + D13X_WDT_RST_SEL);
	sys_write32(D13X_WDT_OP_CFG_SW_CMD0(0), wdt + D13X_WDT_OP);
	sys_write32(D13X_WDT_OP_CFG_SW_CMD1(0), wdt + D13X_WDT_OP);
	sys_write32(D13X_WDT_OP_CNT_CLR_CMD0, wdt + D13X_WDT_OP);
	sys_write32(D13X_WDT_OP_CNT_CLR_CMD1, wdt + D13X_WDT_OP);
	sys_write32(D13X_WDT_CTL_DBG_CONTINUE | D13X_WDT_CTL_ENABLE, wdt + D13X_WDT_CTL);
}
#endif

void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	if (DT_NODE_HAS_STATUS(D13X_CMU_NODE, okay) && DT_NODE_EXISTS(D13X_WDT_NODE)) {
		artinchip_d13x_watchdog_reset();
	}

	for (;;) {
		k_cpu_idle();
	}
}
