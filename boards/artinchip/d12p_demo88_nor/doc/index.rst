.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: artinchip_d12p_demo88_nor

Overview
********

Console baseline derived from Luban-Lite commit
``c06865a38ca8bcffa337eee7b209651abacf45d6``, directory ``target/d12p/demo88-nor``.
This Zephyr port is maintained by UltraSemi.

The console uses UART0 PA0/PA1 at 115200 baud, 8N1, without flow control.
External SPI NOR uses SPI0 on PB0--PB5. MCUboot and the signed application
occupy separate PSRAM regions; the MCUboot entry is ``0x40000100``.
The application is the standard ``samples/hello_world``. Shell and logging
configuration belong to the application.

Building
********

From the west workspace::

   west build --sysbuild -b artinchip_d12p_demo88_nor/d125ccr \
     zephyr-artinchip/samples/hello_world -d build/d12p_demo88_nor_hello -- \
     -DSB_CONFIG_BOOTLOADER_MCUBOOT=y \
     -DSB_CONFIG_MCUBOOT_MODE_RAM_LOAD=y \
     -DSB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y

The optional ArtInChip tools module can package this sysbuild for AirBurn.
The boot chain is ROM -> PBP -> MCUboot -> signed Zephyr (RAM-load).
Vendor ``bootloader.bin`` is used only by the RAM-resident USB updater.
Both image slots are 1 MiB; factory packaging clears the secondary slot.

Dashboard application
*********************

The separate ``zephyr-artinchip-demo-apps/applications/dashboard-1024x600``
application enables the reference EK79007AD MIPI display. It has a 2x2 gauge
region on the left and a title, chart, meters and status lamps on the right.
See that repository's README for build and packaging commands.

Its application-specific overlays set the same RAM-load region in MCUboot and
Zephyr: ``0x40130000..0x401fffff``. The RGB565 framebuffer occupies
``0x40000000..0x4012bfff`` after MCUboot hands off. The baseline Hello World
memory layout must not be used for this display configuration.

Validation scope
****************

On 2026-10-12, the user confirmed the Hello World + shell image passed hardware
testing. This image does not enable a display. The 1024x600 dashboard passed
compilation, RAM-bound checks and offline image verification. The user also
confirmed its hardware test passed, including the new display memory layout.

Exact hashes are recorded in ``zephyr-artinchip-demo-apps/VALIDATION.md``.
NOR write/erase, updates and power-loss recovery require separate tests.
Development signing keys are unsuitable for production.
