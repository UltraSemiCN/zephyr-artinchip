# SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
# SPDX-License-Identifier: Apache-2.0

if(NOT SB_CONFIG_BOOTLOADER_MCUBOOT)
  return()
endif()
if(NOT SB_CONFIG_MCUBOOT_MODE_RAM_LOAD)
  message(FATAL_ERROR "ArtInChip boards require SB_CONFIG_MCUBOOT_MODE_RAM_LOAD=y")
endif()
# RAM-load first boot has no image-manager/update footer consumer.
set_property(TARGET mcuboot PROPERTY MCUBOOT_FOOTER_UPDATE_IMAGES "")
set(board_dir "${ARTINCHIP_DEMO_BOARD_DIR}")
foreach(image mcuboot ${DEFAULT_IMAGE})
  if(image STREQUAL "mcuboot")
    set(fragment mcuboot)
  else()
    set(fragment mcuboot-app)
  endif()
  foreach(kind DTC_OVERLAY CONF)
    if(kind STREQUAL "DTC_OVERLAY")
      set(extension overlay)
    else()
      set(extension conf)
    endif()
    set(var "${image}_EXTRA_${kind}_FILE")
    # Preserve the main application's unprefixed settings unless an image-specific
    # value was explicitly supplied. Creating a prefixed value would hide them.
    if(image STREQUAL "${DEFAULT_IMAGE}" AND NOT DEFINED ${var})
      set(var "EXTRA_${kind}_FILE")
    endif()
    sysbuild_cache_set(VAR ${var} APPEND REMOVE_DUPLICATES
                       "${board_dir}/${fragment}.${extension}")
  endforeach()
endforeach()
