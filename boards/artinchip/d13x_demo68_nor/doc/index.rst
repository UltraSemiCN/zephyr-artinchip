.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: artinchip_d13x_demo68_nor

Overview
********

Console baseline derived from Luban-Lite commit
``c06865a38ca8bcffa337eee7b209651abacf45d6``, directory ``target/d13x/demo68-nor``,
and ``target/configs/d13x_demo68-nor_rt-thread_helloworld_defconfig``.
This Zephyr port is maintained by UltraSemi, not the vendor.

The console and shell use UART1 at 115200 baud.
Application memory: 256 KiB of SRAM at 0x30040000, excluding the SDK bootloader reserve.

Limitations
***********

Only the console baseline is provided; display, storage access, and other
board peripherals require separate enablement and hardware validation.
The board must be started by a suitable vendor loader; no west flash runner
is provided. Compilation does not establish hardware boot support.
The SDK does not fix a package part number; the qualifier selects the
common SoC family implementation.

Building
********

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: artinchip_d13x_demo68_nor/d13x
   :goals: build
   :compact:
