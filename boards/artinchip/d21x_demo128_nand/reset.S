/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

GTEXT(soc_reset_hook)
SECTION_FUNC(TEXT, soc_reset_hook)
	csrci mstatus, 8
	csrw mie, zero
	li t0, ((1 << 22) | (1 << 15))
	csrs 0x7c0, t0
	/* Preserve dirty data inherited from the PBP before disabling D-cache. */
	csrr t0, 0x7c1
	andi t0, t0, 2
	beqz t0, 1f
	.long 0x0030000b /* C906 dcache.ciall */
	fence rw, rw
1:
	csrci 0x7c1, 3
	.long 0x0100000b /* C906 icache.iall */
	fence.i
	ret
