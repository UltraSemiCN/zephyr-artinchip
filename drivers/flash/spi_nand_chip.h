/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZEPHYR_DRIVERS_FLASH_SPI_NAND_CHIP_H_
#define ZEPHYR_DRIVERS_FLASH_SPI_NAND_CHIP_H_

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

struct spi_nand_chip_ops {
	int (*configure)(const struct device *dev);
	int (*ecc_status)(uint8_t status);
};

int spi_nand_update_config(const struct device *dev, uint8_t mask, uint8_t value);
extern const struct spi_nand_chip_ops spi_nand_xt26g01c_ops;

#define SPI_NAND_CHIP_OPS(node_id) \
	COND_CODE_1(DT_NODE_HAS_COMPAT(node_id, xtx_xt26g01c), \
		    (&spi_nand_xt26g01c_ops), (NULL))

#endif /* ZEPHYR_DRIVERS_FLASH_SPI_NAND_CHIP_H_ */
