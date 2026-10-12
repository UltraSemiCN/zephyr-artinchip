# SPDX-License-Identifier: Apache-2.0
if(CONFIG_MCUBOOT)
  zephyr_include_directories(${ZEPHYR_MCUBOOT_MODULE_DIR}/boot/bootutil/include)
  zephyr_sources(../common/mcuboot_handoff.c ../common/mcuboot_reset.S)
endif()
