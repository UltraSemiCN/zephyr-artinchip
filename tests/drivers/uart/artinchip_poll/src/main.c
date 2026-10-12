/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>

static uint32_t elapsed_us;
static uint32_t ready_us;
static uint32_t sleeps;
static uint32_t transmitted;
static bool may_yield;

static uint32_t read_register(mem_addr_t addr)
{
	/* The poll-out path reads LSR only. */
	zassert_equal(addr, 0x14U);
	return elapsed_us >= ready_us ? BIT(5) : 0U;
}

static void write_register(uint32_t value, mem_addr_t addr)
{
	zassert_equal(addr, 0U);
	zassert_equal(value, 'X');
	transmitted++;
}

static void busy_wait(uint32_t us)
{
	elapsed_us += us;
}

static int32_t sleep_ms(int32_t ms)
{
	sleeps++;
	elapsed_us += ms * 1000U;
	return 0;
}

static bool can_yield(void)
{
	return may_yield;
}

LOG_MODULE_REGISTER(uart_artinchip_v11, CONFIG_UART_LOG_LEVEL);
#undef LOG_MODULE_REGISTER
#define LOG_MODULE_REGISTER(...)
#define sys_read32 read_register
#define sys_write32 write_register
#define k_busy_wait busy_wait
#define k_msleep sleep_ms
#define k_can_yield can_yield
#include "uart_artinchip_v11.c"
#undef k_can_yield
#undef k_msleep
#undef k_busy_wait
#undef sys_write32
#undef sys_read32
#undef LOG_MODULE_REGISTER

static const struct artinchip_uart_config config;
static const struct device uart = {.config = &config};

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	ARG_UNUSED(artinchip_uart_init);
	ARG_UNUSED(artinchip_uart_driver_api);
	elapsed_us = 0U;
	ready_us = 0U;
	sleeps = 0U;
	transmitted = 0U;
	may_yield = true;
}

ZTEST(artinchip_uart_poll, test_ready_immediately)
{
	artinchip_uart_poll_out(&uart, 'X');
	zassert_equal(transmitted, 1U);
	zassert_equal(elapsed_us, 0U);
}

ZTEST(artinchip_uart_poll, test_normal_character_does_not_sleep)
{
	ready_us = 87U;
	artinchip_uart_poll_out(&uart, 'X');
	zassert_equal(transmitted, 1U);
	zassert_equal(sleeps, 0U);
	zassert_equal(elapsed_us, ready_us);
}

ZTEST(artinchip_uart_poll, test_stalled_thread_backs_off_and_times_out)
{
	ready_us = UINT32_MAX;
	artinchip_uart_poll_out(&uart, 'X');
	zassert_equal(transmitted, 0U);
	zassert_true(sleeps > 0U);
	zassert_equal(elapsed_us, 4000U);
}

ZTEST(artinchip_uart_poll, test_atomic_context_never_sleeps)
{
	may_yield = false;
	ready_us = UINT32_MAX;
	artinchip_uart_poll_out(&uart, 'X');
	zassert_equal(transmitted, 0U);
	zassert_equal(sleeps, 0U);
	zassert_equal(elapsed_us, 4000U);
}

ZTEST(artinchip_uart_poll, test_recovers_after_backoff)
{
	ready_us = 1500U;
	artinchip_uart_poll_out(&uart, 'X');
	zassert_equal(transmitted, 1U);
	zassert_true(sleeps > 0U);
	zassert_between_inclusive(elapsed_us, ready_us, 4000U);
}

ZTEST_SUITE(artinchip_uart_poll, NULL, NULL, before, NULL, NULL);
