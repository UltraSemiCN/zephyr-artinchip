.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: artinchip_d13x_demo88_nor_secure

Overview
********

Console baseline derived from Luban-Lite commit
``c06865a38ca8bcffa337eee7b209651abacf45d6``, directory ``target/d13x/demo88-nor-secure``,
and ``target/configs/d13x_demo88-nor-secure_rt-thread_helloworld_defconfig``.
This Zephyr port is maintained by UltraSemi, not the vendor.

The console and shell use UART0 at 115200 baud.
Application memory: 768 KiB of SRAM at 0x30040000, excluding the SDK bootloader reserve.

Limitations
***********

Only the console baseline is provided; display, storage access, and other
board peripherals require separate enablement and hardware validation.
The board must be started by a suitable vendor loader; no west flash runner
is provided. Compilation does not establish hardware boot support.
The SDK does not fix a package part number; the qualifier selects the
common SoC family implementation.
The secure suffix tracks the SDK target name only. It does not enable
verified boot, encryption, or key provisioning in this Zephyr baseline.

Building
********

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: artinchip_d13x_demo88_nor_secure/d13x
   :goals: build
   :compact:
