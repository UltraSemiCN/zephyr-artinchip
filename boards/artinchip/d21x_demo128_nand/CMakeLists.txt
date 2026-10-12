# SPDX-License-Identifier: Apache-2.0
if(CONFIG_FLASH_MAP AND CONFIG_SPI_NAND)
  zephyr_sources(../common/airburn_nand_map.c)
endif()
zephyr_sources(reset.S)
if(CONFIG_MCUBOOT)
  zephyr_include_directories(${ZEPHYR_MCUBOOT_MODULE_DIR}/boot/bootutil/include)
  zephyr_sources(mcuboot_handoff.c)
endif()
