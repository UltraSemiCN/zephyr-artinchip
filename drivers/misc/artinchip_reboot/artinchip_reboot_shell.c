/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define ARTINCHIP_REBOOT_REASON_UPGRADE 4U
#define ARTINCHIP_WRI_NODE DT_NODELABEL(wri)
#define ARTINCHIP_WRI_BOOT_INFO 0x100U
#define ARTINCHIP_WRI_REBOOT_REASON GENMASK(7, 4)

void artinchip_soc_set_reboot_reason(uint32_t reason);

static void artinchip_set_reboot_reason(uint32_t reason)
{
#if DT_NODE_HAS_COMPAT(ARTINCHIP_WRI_NODE, artinchip_d12p_wri)
	uintptr_t base = DT_REG_ADDR(ARTINCHIP_WRI_NODE);
	uint32_t boot_info = sys_read32(base + ARTINCHIP_WRI_BOOT_INFO);

	boot_info &= ~ARTINCHIP_WRI_REBOOT_REASON;
	boot_info |= FIELD_PREP(ARTINCHIP_WRI_REBOOT_REASON, reason);
	sys_write32(boot_info, base + ARTINCHIP_WRI_BOOT_INFO);
#else
	artinchip_soc_set_reboot_reason(reason);
#endif
}

static int cmd_uart_aicupg(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	artinchip_set_reboot_reason(ARTINCHIP_REBOOT_REASON_UPGRADE);
	sys_reboot(SYS_REBOOT_COLD);

	CODE_UNREACHABLE;
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_uart_artinchip,
	SHELL_CMD_ARG(aicupg, NULL,
		      "Reboot into ArtInChip BROM upgrade mode.",
		      cmd_uart_aicupg, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(uart, &sub_uart_artinchip, "UART commands.", NULL);
SHELL_CMD_ARG_REGISTER(aicupg, NULL,
		       "Reboot into ArtInChip BROM upgrade mode.",
		       cmd_uart_aicupg, 1, 0);
