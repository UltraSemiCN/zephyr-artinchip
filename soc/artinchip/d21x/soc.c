/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/riscv/csr.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/* Luban-Lite d21x/system.c and core_rv64.h at c06865a38. */
#define C906_PLIC_PERMISSION 0x1ffffc
#define C906_MXSTATUS 0x7c0

void soc_early_init_hook(void)
{
	/* Enable T-Head ISA and memory attributes as required by the vendor. */
	csr_set(C906_MXSTATUS, BIT(22) | BIT(15));
	/* The standard PLIC driver handles priorities, enables, and claims. */
	sys_write32(1, DT_REG_ADDR(DT_NODELABEL(plic)) + C906_PLIC_PERMISSION);
}
