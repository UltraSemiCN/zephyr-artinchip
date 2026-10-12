/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/ztest.h>

/* Exercise the driver against RAM; the DT instance has no init callback. */
#include "clock_control_artinchip.c"

static uint32_t registers[0x1000 / sizeof(uint32_t)];
static struct artinchip_clock_data test_data;
static struct artinchip_clock_config test_config = {
	.base = (uintptr_t)registers,
	.default_uart_rate = 48000000U,
	.display_sclk_rate = 364000000U,
	.display_pixclk_rate = 52000000U,
};
static const struct device test_clock = {
	.config = &test_config,
	.data = &test_data,
};

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(registers, 0, sizeof(registers));
	memset(&test_data, 0, sizeof(test_data));
	test_config.is_d12p = false;
	test_config.is_d21x = false;
	test_config.qspi_rate = 100000000U;
	test_config.display_sclk_rate = 364000000U;
	/* PLL_INT1: 24 MHz * 50, selected as output. */
	registers[0x004 / 4] = BIT(20) | (49U << 8);
}

ZTEST(artinchip_clock, test_uart_recovers_disabled_clock)
{
	uint32_t rate;

	/* PBP disables both gates; reset release must be preserved. */
	registers[0x840 / 4] = BIT(13);
	zassert_ok(artinchip_clock_on(&test_clock, UINT_TO_POINTER(ARTINCHIP_CLK_UART0)));
	zassert_equal(registers[0x840 / 4], BIT(13) | BIT(12) | BIT(8) | 24U);
	zassert_ok(artinchip_clock_get_rate(&test_clock,
					 UINT_TO_POINTER(ARTINCHIP_CLK_UART0), &rate));
	zassert_equal(rate, 48000000U);
	zassert_equal(artinchip_clock_get_status(&test_clock,
		      UINT_TO_POINTER(ARTINCHIP_CLK_UART0)), CLOCK_CONTROL_STATUS_ON);
	zassert_ok(artinchip_clock_off(&test_clock, UINT_TO_POINTER(ARTINCHIP_CLK_UART0)));
	zassert_equal(registers[0x840 / 4], BIT(13) | 24U);
}

ZTEST(artinchip_clock, test_uart_uses_actual_parent)
{
	/* PBP may select 1.008 GHz rather than the DT nominal 1.2 GHz. */
	registers[0x004 / 4] = BIT(20) | (41U << 8);
	zassert_ok(artinchip_clock_on(&test_clock, UINT_TO_POINTER(ARTINCHIP_CLK_UART0)));
	zassert_equal(registers[0x840 / 4] & 0x1fU, 20U);
}

ZTEST(artinchip_clock, test_uart_rejects_unrepresentable_divider)
{
	test_data.uart_rates[0] = 1000000U;
	zassert_equal(artinchip_clock_on(&test_clock,
		      UINT_TO_POINTER(ARTINCHIP_CLK_UART0)), -EINVAL);
	zassert_equal(registers[0x840 / 4], 0U);
}

ZTEST(artinchip_clock, test_de_clock_is_limited)
{
	uint32_t rate;

	zassert_ok(artinchip_clock_on(&test_clock, UINT_TO_POINTER(ARTINCHIP_CLK_DE)));
	zassert_equal(registers[0x8c0 / 4], BIT(12) | BIT(8) | 7U);
	zassert_ok(artinchip_clock_get_rate(&test_clock,
					 UINT_TO_POINTER(ARTINCHIP_CLK_DE), &rate));
	zassert_equal(rate, 150000000U);
}

ZTEST(artinchip_clock, test_lvds_shared_pixel_divider)
{
	uint32_t rate;

	registers[0x220 / 4] = 0x3fffU;
	registers[0x224 / 4] = 0x12345678U;
	zassert_ok(artinchip_clock_on(&test_clock, UINT_TO_POINTER(ARTINCHIP_CLK_LVDS)));
	zassert_equal(registers[0x220 / 4] & 0x3df7U, 6U << 4);
	zassert_equal(registers[0x224 / 4], 0x12345678U, "Touched D12P pixel divider");
	zassert_equal(registers[0x884 / 4], BIT(12) | BIT(8));
	zassert_equal(registers[0xfe8 / 4], 0U, "D13x does not use CMU write authorization");
	zassert_ok(artinchip_clock_get_rate(&test_clock,
					 UINT_TO_POINTER(ARTINCHIP_CLK_LVDS), &rate));
	zassert_within(rate, 364000000U, 100U);
	zassert_ok(artinchip_clock_off(&test_clock, UINT_TO_POINTER(ARTINCHIP_CLK_LVDS)));
	zassert_equal(registers[0x884 / 4], 0U);
}

ZTEST(artinchip_clock, test_lvds_rejects_wrong_serial_ratio)
{
	test_config.display_sclk_rate = 312000000U;
	zassert_equal(artinchip_clock_on(&test_clock,
		      UINT_TO_POINTER(ARTINCHIP_CLK_LVDS)), -ENOTSUP);
	zassert_equal(registers[0x028 / 4], 0U);
}

ZTEST(artinchip_clock, test_lvds_not_supported_on_d12p)
{
	test_config.is_d12p = true;
	zassert_equal(artinchip_clock_on(&test_clock,
		      UINT_TO_POINTER(ARTINCHIP_CLK_LVDS)), -ENOTSUP);
	zassert_equal(registers[0x028 / 4], 0U);
}

ZTEST_SUITE(artinchip_clock, NULL, NULL, before, NULL, NULL);

ZTEST(artinchip_clock, test_d21x_qspi_programs_and_reads_actual_rate)
{
	uint32_t rate;

	test_config.is_d21x = true;
	/* PLL_FRA0: 24 MHz * 25 = 600 MHz; target divider is six. */
	registers[0x20 / 4] = 24U << 8;
	registers[0x460 / 4] = BIT(13);
	zassert_ok(artinchip_clock_on(&test_clock, UINT_TO_POINTER(ARTINCHIP_CLK_QSPI0)));
	zassert_equal(registers[0x460 / 4], BIT(13) | BIT(12) | BIT(8) | 5U);
	zassert_ok(artinchip_clock_get_rate(&test_clock,
					 UINT_TO_POINTER(ARTINCHIP_CLK_QSPI0), &rate));
	zassert_equal(rate, 100000000U);
	zassert_ok(artinchip_clock_off(&test_clock, UINT_TO_POINTER(ARTINCHIP_CLK_QSPI0)));
	zassert_equal(registers[0x460 / 4], BIT(13) | 5U);
}

ZTEST(artinchip_clock, test_d21x_qspi_rejects_invalid_dividers)
{
	test_config.is_d21x = true;
	registers[0x20 / 4] = 24U << 8;
	test_config.qspi_rate = 0U;
	zassert_equal(artinchip_clock_on(&test_clock,
				       UINT_TO_POINTER(ARTINCHIP_CLK_QSPI0)), -EINVAL);
	test_config.qspi_rate = 1000000U;
	zassert_equal(artinchip_clock_on(&test_clock,
				       UINT_TO_POINTER(ARTINCHIP_CLK_QSPI0)), -EINVAL);
	zassert_equal(registers[0x460 / 4], 0U);
}
