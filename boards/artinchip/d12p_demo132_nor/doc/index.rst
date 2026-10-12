.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: artinchip_d12p_demo132_nor

Overview
********

Console baseline derived from Luban-Lite commit
``c06865a38ca8bcffa337eee7b209651abacf45d6``, directory ``target/d12p/demo132-nor``,
and ``target/configs/d12p_demo132-nor_rt-thread_helloworld_defconfig``.
This Zephyr port is maintained by UltraSemi, not the vendor.

The console and shell use UART0 at 115200 baud.
Application memory: 1024 KiB of PSRAM at 0x40000000, using the SDK software-region size.
PBP must initialize PSRAM and the console before loading Zephyr.
The on-chip SRAM window is only 20 KiB.

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
   :board: artinchip_d12p_demo132_nor/d12p
   :goals: build
   :compact:
