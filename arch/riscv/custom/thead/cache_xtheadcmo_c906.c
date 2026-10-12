/*
 * Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/riscv/csr.h>
#include <zephyr/cache.h>
#include <zephyr/sys/util.h>

#define C906_MHCR 0x7c1
#define C906_MHINT 0x7c5
#define MHCR_IE BIT(0)
#define MHCR_DE BIT(1)
#define MHCR_DATA_POLICY GENMASK(8, 2)
#define MHINT_DATA_MASK (BIT(2) | GENMASK(4, 3) | GENMASK(14, 13))
#define MHINT_DATA_VALUE (BIT(2) | BIT(3) | GENMASK(14, 13))
#define MHINT_INSTRUCTION (BIT(8) | BIT(10))

/* C906 cache policy follows LubanLite core_rv64.h (origin/master f2ee295b1a09). */
void arch_icache_enable(void)
{
	if ((csr_read(C906_MHCR) & MHCR_IE) == 0) {
		__asm__ volatile ("fence\n th.icache.iall\n" ::: "memory");
		csr_set(C906_MHCR, MHCR_IE);
	}
	csr_set(C906_MHINT, MHINT_INSTRUCTION);
	__asm__ volatile ("fence\n fence.i\n" ::: "memory");
}

void arch_icache_disable(void)
{
	csr_clear(C906_MHCR, MHCR_IE);
	csr_clear(C906_MHINT, MHINT_INSTRUCTION);
	__asm__ volatile ("fence\n th.icache.iall\n fence.i\n" ::: "memory");
}

void arch_dcache_enable(void)
{
	/* Do not invalidate dirty data when inheriting an enabled cache. */
	if ((csr_read(C906_MHCR) & MHCR_DE) == 0) {
		__asm__ volatile ("fence\n th.dcache.iall\n" ::: "memory");
		csr_set(C906_MHCR, MHCR_DE | MHCR_DATA_POLICY);
	}
	csr_clear(C906_MHINT, MHINT_DATA_MASK);
	csr_set(C906_MHINT, MHINT_DATA_VALUE);
	__asm__ volatile ("fence\n fence.i\n" ::: "memory");
}

void arch_dcache_disable(void)
{
	/* Keep cleaning and disabling in one block: no dirty stack writes between them. */
	if ((csr_read(C906_MHCR) & MHCR_DE) != 0) {
		__asm__ volatile ("fence\n th.dcache.ciall\n fence\n"
				  "csrci 0x7c1, 2\n fence.i\n" ::: "memory");
	}
	csr_clear(C906_MHINT, MHINT_DATA_MASK);
}

void arch_cache_init(void)
{
	if (IS_ENABLED(CONFIG_ICACHE)) {
		arch_icache_enable();
	}
	if (IS_ENABLED(CONFIG_DCACHE)) {
		arch_dcache_enable();
	}
}
