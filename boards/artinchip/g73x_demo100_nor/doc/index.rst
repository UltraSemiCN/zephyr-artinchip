.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: artinchip_g73x_demo100_nor

Overview
********

Console baseline for ``target/g73x/demo100-nor`` in Luban-Lite commit
``c06865a38ca8bcffa337eee7b209651abacf45d6``. This Zephyr port is maintained by
UltraSemi. The SDK target selects the G73X family rather than a fixed
package part number.

The console uses UART0 (PA.0/PA.1) at 115200 baud, 8N1. The loader must
initialize its clock, reset, pin multiplexing, baud rate, and FIFO. The
shell uses polling; native clock/pinctrl initialization is not provided.

Application RAM is limited to ``0xc0000`` bytes at ``0x30040000``.
Source configuration: ``target/configs/g73x_demo100-nor_rt-thread_helloworld_defconfig``.

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
   :board: artinchip_g73x_demo100_nor/g73x
   :goals: build
   :compact:
