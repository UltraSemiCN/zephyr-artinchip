/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>

static struct {
	mem_addr_t addr;
	uint32_t value;
	uint8_t width;
} writes[32];
static size_t count;

static void record(uint32_t value, mem_addr_t addr, uint8_t width)
{
	zassert_true(count < ARRAY_SIZE(writes));
	writes[count].addr = addr;
	writes[count].value = value;
	writes[count++].width = width;
}

static uint8_t read8(mem_addr_t addr)
{
	zassert_equal(addr, 0x2100U);
	return 0x95U;
}

static uint32_t read32(mem_addr_t addr)
{
	zassert_equal(addr, 0x120cU);
	return BIT(7);
}

static void write8(uint8_t value, mem_addr_t addr)
{
	record(value, addr, 1U);
}

static void write32(uint32_t value, mem_addr_t addr)
{
	record(value, addr, 4U);
}

#define sys_read8 read8
#define sys_read32 read32
#define sys_write8 write8
#define sys_write32 write32
#define sys_arch_reboot test_arch_reboot
#include "reboot.c"
#undef sys_arch_reboot
#undef sys_write32
#undef sys_write8
#undef sys_read32
#undef sys_read8

ZTEST(d21x_reboot, test_upgrade_reason_preserves_boot_source)
{
	count = 0U;
	artinchip_soc_set_reboot_reason(4U);
	zassert_equal(count, 3U);
	zassert_equal(writes[0].addr, 0x20fcU);
	zassert_equal(writes[0].value, 0xacU);
	zassert_equal(writes[1].addr, 0x2100U);
	zassert_equal(writes[1].value, 0x45U);
	zassert_equal(writes[2].addr, 0x20fcU);
	zassert_equal(writes[2].value, 0U);
	for (size_t i = 0; i < count; i++) {
		zassert_equal(writes[i].width, 1U, "RTC requires byte accesses");
	}
}

ZTEST(d21x_reboot, test_watchdog_v10_reset_sequence)
{
	static const uint32_t expected[][2] = {
		{0x120c, 0x1180}, {0x120c, 0x1180}, {0x120c, 0x3180},
		{0x30e8, 0xa1c99999}, {0x30e8, 0xa1c66666},
		{0x3008, 0}, {0x3000, 0}, {0x300c, 0xffffffff},
		{0x3040, 0}, {0x3044, 0}, {0x3048, 32000},
		{0x30e8, 0xa1c5a5a0}, {0x30e8, 0xa1ca5a50},
		{0x30e8, 0xa1c55555}, {0x30e8, 0xa1caaaaa}, {0x3000, 3},
	};

	count = 0U;
	artinchip_d21x_watchdog_reset();
	zassert_equal(count, ARRAY_SIZE(expected));
	for (size_t i = 0; i < count; i++) {
		zassert_equal(writes[i].addr, expected[i][0]);
		zassert_equal(writes[i].value, expected[i][1]);
		zassert_equal(writes[i].width, 4U);
		zassert_not_equal(writes[i].addr, 0x3020U, "RST_SEL is v1.1 only");
	}
}

ZTEST_SUITE(d21x_reboot, NULL, NULL, NULL, NULL, NULL);
