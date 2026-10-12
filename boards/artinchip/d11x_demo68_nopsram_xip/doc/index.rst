.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: artinchip_d11x_demo68_nopsram_xip

Overview
********

Console baseline for ``target/d11x/demo68-nopsram-xip`` in Luban-Lite commit
``c06865a38ca8bcffa337eee7b209651abacf45d6``. This Zephyr port is maintained by
UltraSemi. The SDK target selects the D11X family rather than a fixed
package part number.

The console uses UART1 (PA.2/PA.3) at 115200 baud, 8N1. The loader must
initialize its clock, reset, pin multiplexing, baud rate, and FIFO. The
shell uses polling; native clock/pinctrl initialization is not provided.

Application RAM is limited to ``0xc000`` bytes at ``0x30040000``.
Source configuration: ``target/configs/d11x_demo68-nopsram-xip_rt-thread_helloworld_defconfig``.

The board has no PSRAM. Code executes at 0x60080100 in the vendor-configured
QSPI XIP window, following a 256-byte AIC header; data and BSS use 48 KiB
SRAM. A raw Zephyr binary cannot replace the vendor AIC container.

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
   :board: artinchip_d11x_demo68_nopsram_xip/d11x
   :goals: build
   :compact:
