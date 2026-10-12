/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/ztest.h>

#include "spi_nand_chip.h"

static int read_error;
static int backend_read(const struct device *dev, off_t offset, void *data, size_t size)
{
	ARG_UNUSED(dev);
	if (read_error != 0) {
		return read_error;
	}
	memset(data, offset / 0x20000, size);
	return 0;
}

static int backend_ex_op(const struct device *dev, uint16_t op, uintptr_t in, void *out)
{
	off_t block = *(const off_t *)in / 0x20000;

	ARG_UNUSED(dev);
	if (op != FLASH_EX_OP_IS_BAD_BLOCK) {
		return -ENOTSUP;
	}
	*(enum flash_block_status *)out =
		(block == 0 || block == 2 || block == 5) ? FLASH_BLOCK_BAD : FLASH_BLOCK_GOOD;
	return 0;
}
static DEVICE_API(flash, backend_api) = {
	.read = backend_read,
	.ex_op = backend_ex_op,
};
DEVICE_DT_DEFINE(DT_NODELABEL(test_nand), NULL, NULL, NULL, NULL,
		 POST_KERNEL, 80, &backend_api);

static const struct device *const map = DEVICE_DT_GET(DT_NODELABEL(test_map));

ZTEST(nand_map, test_skips_bad_blocks_and_restarts_per_partition)
{
	uint8_t data[4];

	zassert_true(device_is_ready(map));
	zassert_ok(flash_read(map, 0x1fffe, data, sizeof(data)));
	zassert_equal(data[0], 1);
	zassert_equal(data[1], 1);
	zassert_equal(data[2], 3);
	zassert_equal(data[3], 3);
	zassert_ok(flash_read(map, 0x80000, data, sizeof(data)));
	zassert_equal(data[0], 4);
	zassert_ok(flash_read(map, 0xa0000, data, sizeof(data)));
	zassert_equal(data[0], 6);
}

ZTEST(nand_map, test_capacity_and_partition_bounds)
{
	uint8_t data[4];

	zassert_equal(flash_read(map, 0x40000, data, 1), -ENOSPC);
	zassert_equal(flash_read(map, 0x7ffff, data, 2), -EINVAL);
	zassert_equal(flash_read(map, -1, data, 1), -EINVAL);
	zassert_equal(flash_read(map, 0x100000, data, 1), -EINVAL);
	zassert_equal(flash_read(map, 0xe0000, data, 1), -ENOSPC);
}

ZTEST(nand_map, test_propagates_ecc_failure)
{
	uint8_t data;

	read_error = -EBADMSG;
	zassert_equal(flash_read(map, 0, &data, 1), -EBADMSG);
	read_error = 0;
}

ZTEST(nand_map, test_read_only)
{
	uint8_t data = 0;

	zassert_equal(flash_write(map, 0, &data, 1), -ENOTSUP);
	zassert_equal(flash_erase(map, 0, 0x20000), -ENOTSUP);
}

ZTEST_SUITE(nand_map, NULL, NULL, NULL, NULL, NULL);

static uint8_t chip_status;
static uint8_t chip_config = BIT(3);
static bool otp_requested;
static uint8_t fail_command;

static int test_spi_transceive(const struct device *dev, const struct spi_config *config,
			       const struct spi_buf_set *tx, const struct spi_buf_set *rx)
{
	const uint8_t *command = tx->buffers[0].buf;

	ARG_UNUSED(dev);
	ARG_UNUSED(config);
	if (command[0] == fail_command) {
		return -EIO;
	}
	if (command[0] == 0x1f && command[1] == 0xb0) {
		chip_config = command[2];
		otp_requested |= (chip_config & BIT(6)) != 0U;
	}
	if (rx != NULL) {
		for (size_t i = 0U; i < rx->count; i++) {
			uint8_t *out = rx->buffers[i].buf;

			if (out == NULL) {
				continue;
			}
			memset(out, 0xa5, rx->buffers[i].len);
			if (command[0] == 0x9f) {
				out[0] = 0x0b;
				out[1] = 0x11;
			} else if (command[0] == 0x0f) {
				out[2] = command[1] == 0xb0 ? chip_config : chip_status;
			}
		}
	}
	return 0;
}
static DEVICE_API(spi, test_spi_api) = { .transceive = test_spi_transceive };
DEVICE_DT_DEFINE(DT_NODELABEL(xtx_test_bus), NULL, NULL, NULL, NULL,
		 POST_KERNEL, 70, &test_spi_api);

ZTEST(nand_map, test_xt26g01c_ecc_decode)
{
	const struct device *chip = DEVICE_DT_GET(DT_NODELABEL(xtx_test));
	uint8_t data;

	zassert_true(device_is_ready(chip));
	zassert_equal(chip_config, BIT(3) | BIT(4));
	zassert_false(otp_requested);
	/* In particular, 2 corrected bits must not be decoded as uncorrectable. */
	for (uint8_t corrected = 0U; corrected <= 8U; corrected++) {
		chip_status = corrected << 4;
		zassert_ok(flash_read(chip, corrected * 2048, &data, 1));
		zassert_equal(data, 0xa5);
	}
	chip_status = 0xf0;
	zassert_equal(flash_read(chip, 9 * 2048, &data, 1), -EBADMSG);
	chip_status = 0;
}

ZTEST(nand_map, test_xt26g01c_config_errors)
{
	const struct device *chip = DEVICE_DT_GET(DT_NODELABEL(xtx_test));
	uint8_t saved_config = chip_config;
	int ret;

	fail_command = 0x0f;
	ret = spi_nand_update_config(chip, BIT(4), BIT(4));
	fail_command = 0;
	zassert_equal(ret, -EIO);
	zassert_equal(chip_config, saved_config);
	fail_command = 0x1f;
	ret = spi_nand_update_config(chip, BIT(4), BIT(4));
	fail_command = 0;
	zassert_equal(ret, -EIO);
	zassert_equal(chip_config, saved_config);
}
