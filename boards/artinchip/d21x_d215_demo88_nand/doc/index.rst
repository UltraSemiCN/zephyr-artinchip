.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: artinchip_d21x_d215_demo88_nand

Overview
********

Console baseline for ``target/d21x/d215-demo88-nand`` in Luban-Lite commit
``c06865a38ca8bcffa337eee7b209651abacf45d6``. This Zephyr port is maintained by
UltraSemi. The SDK target selects the D21X family rather than a fixed
package part number.

The console uses UART0 (PA.0/PA.1) at 115200 baud, 8N1. The loader must
initialize its clock, reset, pin multiplexing, baud rate, and FIFO. The
shell uses polling; native clock/pinctrl initialization is not provided.

Application RAM is limited to ``0x1000000`` bytes at ``0x40000000``.
Source configuration: ``target/configs/d21x_d215-demo88-nand_rt-thread_helloworld_defconfig``.

The baseline uses the first 16 MiB of initialized DRAM, below the vendor
bootloader region. It requires RV64 machine-mode entry on hart 0; an SBI
supervisor-mode boot is not supported. The counter is read via CSR time
and MTIMECMP is at 0x24004000. PLIC external interrupts use machine context 0.

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
   :board: artinchip_d21x_d215_demo88_nand/d21x
   :goals: build
   :compact:
