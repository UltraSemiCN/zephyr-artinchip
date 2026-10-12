/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/cache.h>
#include <zephyr/irq.h>
#include <bootutil/mcuboot_status.h>

void mcuboot_status_change(mcuboot_status_type_t status)
{
	if (status == MCUBOOT_STATUS_BOOTABLE_IMAGE_FOUND) {
		(void)irq_lock();
		for (unsigned int irq = 0; irq < CONFIG_NUM_IRQS; irq++) {
			irq_disable(irq);
		}
		/* Commit the copied image before disabling caches for the next stage. */
		sys_cache_data_flush_all();
		sys_cache_data_disable();
		sys_cache_instr_disable();
		__asm__ volatile ("fence rw, rw\n fence.i" ::: "memory");
	}
}
