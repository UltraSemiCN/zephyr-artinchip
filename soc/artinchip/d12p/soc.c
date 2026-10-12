/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define D12P_CMU_NODE DT_NODELABEL(cmu)
#define D12P_WDT_NODE DT_NODELABEL(wdt)

/*
 * D12p CMU module clocks (CLK_WDT_REG and friends) are write-protected: every
 * write to the module clock register must first be authorized through the
 * separate write-config register CLK_WR_CFG_REG (0x0FE8). The authorization is
 * armed by writing (key << key_shift) | <target register offset> and is
 * granted once the grant bit reads back set. This matches the AUTHCLK model in
 * luban-lite (bsp/artinchip/hal/cmu/v2/aic_hal_auth_clk.c); the older D13x/D12x
 * style of stuffing the key into the module register itself does NOT work here
 * and silently drops the write, leaving the watchdog unclocked so the reboot
 * never happens.
 */
#define D12P_CMU_WDT_CLK_REG      0x020CU
#define D12P_CMU_WR_CFG_REG       0x0FE8U
#define D12P_CMU_AUTH_KEY         0xA1CU
#define D12P_CMU_AUTH_KEY_SHIFT   20U
#define D12P_CMU_AUTH_GRANT       BIT(16)
#define D12P_CMU_MOD_CLK_EN       BIT(16)
#define D12P_CMU_MOD_BUS_EN       BIT(20)
#define D12P_CMU_MOD_RSTN         BIT(24)

#define D12P_WDT_CTL              0x000U
#define D12P_WDT_IRQ_EN           0x008U
#define D12P_WDT_IRQ_STA          0x00CU
#define D12P_WDT_IRQ_THD          0x044U
#define D12P_WDT_RST_THD          0x048U
#define D12P_WDT_OP               0x0E8U
#define D12P_WDT_CTL_DBG_CONTINUE BIT(1)
#define D12P_WDT_CTL_ENABLE       BIT(0)
#define D12P_WDT_OP_WR_EN_CMD0    0xA1C99999U
#define D12P_WDT_OP_WR_EN_CMD1    0xA1C66666U
#define D12P_WDT_OP_CNT_CLR_CMD0  0xA1C55555U
#define D12P_WDT_OP_CNT_CLR_CMD1  0xA1CAAAAAU
#define D12P_WDT_OP_CFG_SW_CMD0(n) (0xA1C5A5A0U | (n))
#define D12P_WDT_OP_CFG_SW_CMD1(n) (0xA1CA5A50U | (n))
#define D12P_WDT_SEC_TO_CNT(n)    ((n) * 32000U)

#if DT_NODE_HAS_STATUS(D12P_CMU_NODE, okay) && DT_NODE_EXISTS(D12P_WDT_NODE)
static void artinchip_d12p_cmu_auth_write(uintptr_t cmu, uint32_t reg, uint32_t val)
{
	uint32_t auth = (D12P_CMU_AUTH_KEY << D12P_CMU_AUTH_KEY_SHIFT) | reg;
	int i;

	sys_write32(auth, cmu + D12P_CMU_WR_CFG_REG);

	/* Wait for the write authorization to be granted (~100us in luban-lite). */
	for (i = 0; i < 1000; i++) {
		if (sys_read32(cmu + D12P_CMU_WR_CFG_REG) & D12P_CMU_AUTH_GRANT) {
			break;
		}
		k_busy_wait(1);
	}

	sys_write32(val, cmu + reg);
}

static void artinchip_d12p_watchdog_reset(void)
{
	uintptr_t cmu = DT_REG_ADDR(D12P_CMU_NODE);
	uintptr_t wdt = DT_REG_ADDR(D12P_WDT_NODE);
	uint32_t clk;

	/* Ungate the watchdog module and bus clocks (one authorized write each). */
	clk = sys_read32(cmu + D12P_CMU_WDT_CLK_REG);
	clk |= D12P_CMU_MOD_CLK_EN;
	artinchip_d12p_cmu_auth_write(cmu, D12P_CMU_WDT_CLK_REG, clk);
	clk |= D12P_CMU_MOD_BUS_EN;
	artinchip_d12p_cmu_auth_write(cmu, D12P_CMU_WDT_CLK_REG, clk);

	/* Pulse the module reset so the watchdog starts from a known state. */
	clk &= ~D12P_CMU_MOD_RSTN;
	artinchip_d12p_cmu_auth_write(cmu, D12P_CMU_WDT_CLK_REG, clk);
	k_busy_wait(30);
	clk |= D12P_CMU_MOD_RSTN;
	artinchip_d12p_cmu_auth_write(cmu, D12P_CMU_WDT_CLK_REG, clk);
	k_busy_wait(30);

	sys_write32(D12P_WDT_OP_WR_EN_CMD0, wdt + D12P_WDT_OP);
	sys_write32(D12P_WDT_OP_WR_EN_CMD1, wdt + D12P_WDT_OP);
	sys_write32(0U, wdt + D12P_WDT_IRQ_EN);
	sys_write32(0U, wdt + D12P_WDT_CTL);
	sys_write32(0xffffffffU, wdt + D12P_WDT_IRQ_STA);
	sys_write32(0U, wdt + D12P_WDT_IRQ_THD);
	sys_write32(D12P_WDT_SEC_TO_CNT(1), wdt + D12P_WDT_RST_THD);
	sys_write32(D12P_WDT_OP_CFG_SW_CMD0(0), wdt + D12P_WDT_OP);
	sys_write32(D12P_WDT_OP_CFG_SW_CMD1(0), wdt + D12P_WDT_OP);
	sys_write32(D12P_WDT_OP_CNT_CLR_CMD0, wdt + D12P_WDT_OP);
	sys_write32(D12P_WDT_OP_CNT_CLR_CMD1, wdt + D12P_WDT_OP);
	sys_write32(D12P_WDT_CTL_DBG_CONTINUE | D12P_WDT_CTL_ENABLE, wdt + D12P_WDT_CTL);
}
#endif

void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	if (DT_NODE_HAS_STATUS(D12P_CMU_NODE, okay) && DT_NODE_EXISTS(D12P_WDT_NODE)) {
		artinchip_d12p_watchdog_reset();
	}

	for (;;) {
		k_cpu_idle();
	}
}
