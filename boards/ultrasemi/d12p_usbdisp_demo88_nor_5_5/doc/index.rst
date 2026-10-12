.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: ultrasemi_d12p_usbdisp_demo88_nor_5_5

Overview
********

Designed by UltraSemi, this board uses an ArtInChip D12P-series D125 SoC
and a 5.5-inch 720x1280 MIPI DSI display. It is based on the Luban-Lite
``d12p/usbdisp-demo88-nor`` design.

UART0 uses PA0/PA1 at 115200 8N1. External SPI NOR uses PB0--PB5.
Zephyr initializes UART and QSPI through clock, reset and pinctrl drivers.
Vendor PBP initializes PSRAM and parent PLLs.

Build and package
*****************

The supported boot chain is PBP -> MCUboot -> signed Zephyr (RAM-load).
The following command builds the standard LVGL sample. The custom dashboard
is maintained separately at
``zephyr-artinchip-demo-apps/applications/dashboard-720x1280``.
Its README contains the dashboard build and automatic packaging commands.
Run these commands from the west workspace containing ``zephyr-artinchip``
and ``zephyr-artinchip-tools``::

   west build --sysbuild -p always \
     -b ultrasemi_d12p_usbdisp_demo88_nor_5_5/d125ccv \
     zephyr-artinchip/samples/subsys/display/lvgl \
     -d build/ultrasemi_d12p_usbdisp_demo88_nor_5_5_lvgl -- \
     -DSB_CONFIG_BOOTLOADER_MCUBOOT=y \
     -DSB_CONFIG_MCUBOOT_MODE_RAM_LOAD=y \
     -DSB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y

   python zephyr-artinchip-tools/scripts/mk_image_zephyr.py \
     --build-dir build/ultrasemi_d12p_usbdisp_demo88_nor_5_5_lvgl

The tools module packages the image for ArtInChip AirBurn under
``build/ultrasemi_d12p_usbdisp_demo88_nor_5_5_lvgl/airburn/``.
It reads board identity and partitions from build metadata. The vendor USB updater
runs in RAM during flashing;
MCUboot is the persistent boot target. Building does not require the tools module.

MCUboot supplies a development signing key. Set
``SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`` to select a production key.

Validation
**********

Boot and shell operation have been verified on a D125CCS unit with
16 MiB PSRAM using the ``d125ccv`` build qualifier. On 2026-10-12, the user
confirmed the earlier 720x1280 dashboard image passed hardware testing.
The user also confirmed the separately named ``dashboard-720x1280`` build
passed hardware testing after the layout changes.

Exact image hashes and results are recorded in the demo repository's
``VALIDATION.md``. A passing application test does not establish NOR write/erase,
update or power-loss recovery behavior.
