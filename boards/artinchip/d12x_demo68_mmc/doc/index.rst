.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: artinchip_d12x_demo68_mmc

Overview
********

Console baseline for ``target/d12x/demo68-mmc`` in Luban-Lite commit
``c06865a38ca8bcffa337eee7b209651abacf45d6``. This Zephyr port is maintained by
UltraSemi. The SDK target selects the D12X family rather than a fixed
package part number.

The console uses UART1 (PA.2/PA.3) at 115200 baud, 8N1. The loader must
initialize its clock, reset, pin multiplexing, baud rate, and FIFO. The
shell uses polling; native clock/pinctrl initialization is not provided.

Application RAM is limited to ``0x400000`` bytes at ``0x40000000``.
Source configuration: ``target/configs/d12x_demo68-mmc_rt-thread_helloworld_defconfig``.

The application uses the lower PSRAM CMA region from the SDK memory map.
PBP must initialize PSRAM before loading the image.

Limitations
***********

Compile-tested baseline only; hardware boot, timer cadence, and UART input
require validation. Runtime NOR/NAND/eMMC, display, USB, DMA and other board
peripherals are not enabled. No AirBurn profile or west flash runner is
added by these board files. Factory images require the matching loader and
media-specific packaging.

Building
********

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: artinchip_d12x_demo68_mmc/d12x
   :goals: build
   :compact:
