/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

/* Called by the standard RISC-V __start before CLIC setup or stack setup.
 * No stack, BSS, C calls, or T-head instructions may be assumed here.
 */

GTEXT(soc_reset_hook)
SECTION_FUNC(TEXT, soc_reset_hook)
	csrci mstatus, 8
	/* ArtInChip E907 SystemInit: THEADISAEE and MM in MXSTATUS. */
	li t0, ((1 << 22) | (1 << 15))
	csrs 0x7c0, t0
	ret
