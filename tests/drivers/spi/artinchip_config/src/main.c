/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/logging/log.h>

/* Preinclude GPIO CS helpers before the driver's DT instance optimization. */
LOG_MODULE_REGISTER(spi_artinchip, CONFIG_SPI_LOG_LEVEL);
#include "spi_context.h"

/* No controller instance is enabled: test configuration against RAM registers. */
#undef LOG_MODULE_REGISTER
#define LOG_MODULE_REGISTER(...)
#include "spi_artinchip.c"
#undef LOG_MODULE_REGISTER

static uint32_t registers[16];
static struct artinchip_qspi_data controller_data;
static struct artinchip_qspi_config controller_config = {
	.base = (uintptr_t)registers,
};
static const struct device controller = {
	.config = &controller_config,
	.data = &controller_data,
};

static struct spi_config transfer = {
	.frequency = 12000000U,
};

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	/* Controller initialization is outside this register-backed configuration test. */
	ARG_UNUSED(artinchip_qspi_init);
	memset(registers, 0, sizeof(registers));
	memset(&controller_data, 0, sizeof(controller_data));
	k_sem_init(&controller_data.ctx.lock, 1, 1);
	k_sem_init(&controller_data.ctx.sync, 0, 1);
	controller_config.fixed_div2 = false;
	controller_data.input_hz = 198000000U;
	memset(&transfer, 0, sizeof(transfer));
	transfer.frequency = 12000000U;
	transfer.operation = SPI_WORD_SET(8);
}

static void check_rejected(spi_operation_t mode)
{
	uint32_t saved[ARRAY_SIZE(registers)];

	memset(registers, 0xa5, sizeof(registers));
	memcpy(saved, registers, sizeof(saved));
	transfer.operation |= mode;
	zassert_equal(artinchip_qspi_configure(&controller, &transfer), -ENOTSUP);
	zassert_mem_equal(registers, saved, sizeof(saved), "Rejected mode changed registers");
	zassert_is_null(controller_data.ctx.config, "Rejected mode became active");
}

ZTEST(artinchip_spi_config, test_reject_loopback)
{
	check_rejected(SPI_MODE_LOOP);
}

ZTEST(artinchip_spi_config, test_reject_half_duplex)
{
	check_rejected(SPI_HALF_DUPLEX);
}

ZTEST(artinchip_spi_config, test_reject_combined_modes)
{
	check_rejected(SPI_MODE_LOOP | SPI_HALF_DUPLEX);
}

ZTEST(artinchip_spi_config, test_accept_full_duplex)
{
	zassert_ok(artinchip_qspi_configure(&controller, &transfer));
	zassert_equal(controller_data.ctx.config, &transfer);
	zassert_equal(controller_data.frequency, transfer.frequency);
}

ZTEST(artinchip_spi_config, test_fixed_divider_low_frequency)
{
	controller_config.fixed_div2 = true;
	transfer.frequency = 4000U;
	zassert_ok(artinchip_qspi_configure(&controller, &transfer));
}

ZTEST(artinchip_spi_config, test_zero_frequency)
{
	transfer.frequency = 0U;
	check_rejected(0);
}

ZTEST(artinchip_spi_config, test_below_minimum_frequency)
{
	transfer.frequency = 4000U;
	check_rejected(0);
}

static void use_gpio_cs(void)
{
	transfer.cs.cs_is_gpio = true;
	transfer.cs.gpio = (struct gpio_dt_spec) {
		.port = DEVICE_DT_GET(DT_NODELABEL(test_gpio)),
		.pin = 0,
		.dt_flags = GPIO_ACTIVE_LOW,
	};
	zassert_true(gpio_is_ready_dt(&transfer.cs.gpio));
	zassert_ok(gpio_pin_configure_dt(&transfer.cs.gpio, GPIO_OUTPUT_INACTIVE));
}

ZTEST(artinchip_spi_config, test_timeout_releases_gpio_and_lock)
{
	uint8_t byte = 0;
	struct spi_buf buffer = {.buf = &byte, .len = sizeof(byte)};
	const struct spi_buf_set buffers = {.buffers = &buffer, .count = 1};

	use_gpio_cs();
	transfer.operation |= SPI_HOLD_ON_CS | SPI_LOCK_ON;
	/* RAM registers never acknowledge the FIFO reset, producing a timeout. */
	zassert_equal(artinchip_qspi_transceive(&controller, &transfer, &buffers, NULL),
		      -ETIMEDOUT);
	zassert_equal(gpio_emul_output_get(transfer.cs.gpio.port, 0), 1,
		      "Active-low CS remained asserted after timeout");
	zassert_equal(k_sem_count_get(&controller_data.ctx.lock), 1,
		      "Bus remained locked after timeout");
}

ZTEST(artinchip_spi_config, test_invalid_mode_releases_lock)
{
	transfer.operation |= SPI_MODE_LOOP | SPI_LOCK_ON;
	/* Exercise a retained owner, as with a preceding locked transfer. */
	controller_data.ctx.config = &transfer;
	controller_data.ctx.owner = &transfer;
	zassert_ok(k_sem_take(&controller_data.ctx.lock, K_NO_WAIT));
	zassert_equal(artinchip_qspi_transceive(&controller, &transfer, NULL, NULL),
		      -ENOTSUP);
	zassert_equal(k_sem_count_get(&controller_data.ctx.lock), 1);
}

ZTEST(artinchip_spi_config, test_success_preserves_hold_until_release)
{
	use_gpio_cs();
	transfer.operation |= SPI_HOLD_ON_CS | SPI_LOCK_ON;
	zassert_ok(artinchip_qspi_transceive(&controller, &transfer, NULL, NULL));
	zassert_equal(gpio_emul_output_get(transfer.cs.gpio.port, 0), 0);
	zassert_equal(k_sem_count_get(&controller_data.ctx.lock), 0);
	zassert_ok(artinchip_qspi_release(&controller, &transfer));
	zassert_equal(gpio_emul_output_get(transfer.cs.gpio.port, 0), 1);
	zassert_equal(k_sem_count_get(&controller_data.ctx.lock), 1);
}

ZTEST_SUITE(artinchip_spi_config, NULL, NULL, before, NULL, NULL);
