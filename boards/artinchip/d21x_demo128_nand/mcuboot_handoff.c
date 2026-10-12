/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */
#include <bootutil/mcuboot_status.h>
#include <zephyr/arch/riscv/csr.h>
#include <zephyr/cache.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>

void mcuboot_status_change(mcuboot_status_type_t status)
{
	if (status == MCUBOOT_STATUS_BOOTABLE_IMAGE_FOUND) {
		(void)irq_lock();
		csr_write(mie, 0);
		/* C906 machine-context PLIC enables; leave supervisor context unused. */
		for (uint32_t word = 0U; word < 3U; word++) {
			sys_write32(0U, DT_REG_ADDR(DT_NODELABEL(plic)) + 0x2000U + word * 4U);
		}
		/* Write back the loaded image and leave caches disabled for the next stage. */
		sys_cache_data_disable();
		sys_cache_instr_disable();
		__asm__ volatile ("fence rw, rw\n fence.i" ::: "memory");
	}
}
