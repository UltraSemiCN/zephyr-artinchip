.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: artinchip_d13x_demo88_nor

Overview
********

Console baseline derived from Luban-Lite commit
``c06865a38ca8bcffa337eee7b209651abacf45d6``, directory ``target/d13x/demo88-nor``.
This Zephyr port is maintained by UltraSemi.

The console uses UART0 PA0/PA1 at 115200 baud, 8N1, without flow control.
External SPI NOR uses SPI0 on PB0--PB5. MCUboot runs in PBP-initialized PSRAM at ``0x40000100``, clear of the
PBP SRAM workspace. The baseline signed application runs in SRAM.
The application is the standard ``samples/hello_world``. Shell and logging
configuration belong to the application.

Building
********

From the west workspace::

   west build --sysbuild -b artinchip_d13x_demo88_nor/d13x \
     zephyr-artinchip/samples/hello_world -d build/d13x_demo88_nor_hello -- \
     -DSB_CONFIG_BOOTLOADER_MCUBOOT=y \
     -DSB_CONFIG_MCUBOOT_MODE_RAM_LOAD=y \
     -DSB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y

The optional ArtInChip tools module can package this sysbuild for AirBurn.
The boot chain is ROM -> PBP -> MCUboot -> signed Zephyr (RAM-load).
Vendor ``bootloader.bin`` is used only by the RAM-resident USB updater.
Both image slots are 1 MiB; factory packaging clears the secondary slot.

Dashboard application
*********************

``zephyr-artinchip-demo-apps/applications/dashboard-1024x600`` includes a
D133 demo88 NOR profile for a 1024x600 VESA 24-bit LVDS display.
Single link 0 uses PD18--PD27 and panel enable uses PE13.
Pixel and serial clocks are 52 MHz and 364 MHz, respectively. Build commands
and image validation records are maintained in that application repository.

PBP initializes PSRAM before MCUboot runs at ``0x40000100`` and loads the
application at ``0x40130000``. After handoff, the application reuses MCUboot
memory for the framebuffer. The framebuffer occupies
``0x40000000..0x4012bfff`` and the application occupies
``0x40130000..0x401fffff``. Both images use the same RAM-load bounds.
The baseline Hello World configuration remains in SRAM with no display.

Validation scope
****************

The normal MCUboot LVDS dashboard booted and displayed correctly on hardware
on October 12, 2026. The tested image hash is recorded in the demo application
repository. This result does not validate other boards or the separate
Hello World configuration. Repeated cold boot and interactive UART checks
remain to be recorded for this image.
Flash updates, write/erase tests and power-loss
recovery are not validated. Development signing keys are unsuitable for production.

The UART clock driver restores the D13x module and bus gates after PBP disables
them. It reads PLL_INT1 to calculate the baud-rate divider. Temporary entry
markers and raw UART restoration wrappers are not part of the boot chain.
