/*
 * Copyright (c) 2026 UltraSemi
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include "spi_nand.h"
#include "spi_nand_chip.h"

static int xt26g01c_configure(const struct device *dev)
{
	/* Geometry comes from DT; preserve unrelated configuration bits. */
	return spi_nand_update_config(dev, SPI_NAND_FEATURE_CONFIG_ECC_EN,
				      SPI_NAND_FEATURE_CONFIG_ECC_EN);
}

static int xt26g01c_ecc_status(uint8_t status)
{
	/* LubanLite origin/master f2ee295b1a09: bsp/peripheral/spinand/xtx.c. */
	return FIELD_GET(GENMASK(7, 4), status) == 0xf ? -EBADMSG : 0;
}

const struct spi_nand_chip_ops spi_nand_xt26g01c_ops = {
	.configure = xt26g01c_configure,
	.ecc_status = xt26g01c_ecc_status,
};
