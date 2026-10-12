.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: artinchip_d21x_demo128_nand

Overview
********

Board support for ``target/d21x/demo128-nand`` in Luban-Lite commit
``c06865a38ca8bcffa337eee7b209651abacf45d6``. This Zephyr port is maintained by
UltraSemi. The SDK target selects the D21X family rather than a fixed
package part number.

The hardware selected for bring-up is D213EC-V4: QFN128, 128 MiB DDR3,
XT26G01CW SPI NAND and LVDS. The vendor DDR3 profile specifies 600 MHz.
The standalone 16 MiB application region is an allocation limit, not the
physical DDR capacity. PBP/MCUboot NAND boot, Dashboard LVDS display and
Shell have been validated on this board.

The vendor driver identifies XT26G01C with manufacturer ID ``0x0b`` and device
ID ``0x11``. Its geometry is 128 MiB, 2048-byte pages with 128-byte OOB,
64 pages per erase block and 1024 erase blocks. The expected erase-block size
is 128 KiB. These values come from the vendor chip table; the Zephyr driver
checks the JEDEC ID during initialization. ECC status uses bits 7:4, with ``0xf``
indicating an uncorrectable error. The 64-byte OOB template for other NAND
parts must not be used for this device.

The vendor pinmux assigns SPI0 to PB0--PB5, function 3. The reference LVDS
panel enable is PC7, unlike PE13 on D133 demo88.

The console uses UART0 (PA.0/PA.1) at 115200 baud, 8N1. Zephyr initializes
its clock, reset, pin multiplexing, baud rate, and FIFO. The shell uses
polling. QSPI clock, reset and pin multiplexing are also initialized by Zephyr.

Application RAM is limited to ``0x1000000`` bytes at ``0x40000000``.
Source configuration: ``target/configs/d21x_demo128-nand_rt-thread_helloworld_defconfig``.

The baseline uses the first 16 MiB of initialized DRAM, below the vendor
bootloader region. It requires RV64 machine-mode entry on hart 0; an SBI
supervisor-mode boot is not supported. The counter is read via CSR time
and MTIMECMP is at 0x24004000. Its MMIO requires 32-bit accesses even on RV64. PLIC external interrupts use machine context 0.

Limitations
***********

NAND boot through MCUboot into an interactive shell is hardware verified.
Exact timer cadence and bad-block recovery remain separate validation items.
Display, USB and eMMC are not enabled in the Shell test image.
The AirBurn NAND mapping is read-only: runtime updates and NFTL are not
supported. The physical SPI NAND driver exposes raw addressing; MCUboot uses
the separate mapped device. Images are installed by the vendor USB updater.

MCUboot
*******

Standard sysbuild with RAM-load uses the PBP to initialize DDR and load
MCUboot at ``0x41000100``. The signed application is loaded at ``0x40200000``
with an 8 MiB runtime region. MCUboot and the Dashboard enable C906 caches.
MCUboot writes back data and disables caches before chainload; C906 entry also
cleans inherited dirty data before disabling D-cache.
The E907 cache implementation is not used.

The NAND layout reserves 1 MiB for SPL, 256 KiB each for the environment
copies, and 4 MiB each for the primary and secondary slots. The mapped flash
device skips bad blocks independently within each partition, matching the
AirBurn MTD writer. Reads cannot consume blocks from the next partition.
Uncorrectable ECC errors abort reads. The packer reserves at least two erase
blocks in the primary slot and clears the secondary header during factory
installation.

Use ``zephyr-artinchip-demo-apps/applications/boot-shell`` as the test
application with ``--sysbuild`` and package the build with
``zephyr-artinchip-tools/scripts/mk_image_zephyr.py``. The tools repository
contains the matching D213 DDR and USB updater resources. The target chain
is PBP, MCUboot, then signed Zephyr; the vendor updater runs only in RAM.

Building
********

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: artinchip_d21x_demo128_nand/d21x
   :goals: build
   :compact:

Validation
**********

On 2026-10-12, the user confirmed board testing of
``dashboard_1024x600_d213_lvds`` with XT26G01CW NAND and 1024x600 LVDS.
The cached MCUboot authentication function took approximately 0.573 seconds
between JTAG timestamps, including debugger overhead. This is not the total
cold-boot time. Image hashes are recorded in the demo-apps VALIDATION.md.

ROM USB upgrade
***************

With ``CONFIG_SHELL=y`` and ``CONFIG_ARTINCHIP_REBOOT_SHELL=y``, run ``aicupg``
(or ``uart aicupg``) to request ROM USB upgrade mode. This records reason 4 in
RTC backup register bits 7:4, preserving the boot-source bits, then resets the
SoC with watchdog v1.0. The RTC backup register requires byte writes and the
``0xac`` write-enable key. The v1.1 watchdog reset-selector register is not used.
This command reboots into the vendor updater; Zephyr does not implement the
AirBurn USB transport.

The register sequence follows LubanLite ``origin/master`` commit
``f2ee295b1a098b4f6e7d9e78133ed972b58d4ce7``, specifically
``bsp/artinchip/include/hal/hal_wri.h``, ``bsp/artinchip/hal/wdt/hal_wdt.c`` and
the D21x clock/reset definitions. Register-sequence tests and the D213 Dashboard
build pass. The user subsequently reported testing OK for the D213 update.
No additional USB trace or reset timing measurements were supplied.
