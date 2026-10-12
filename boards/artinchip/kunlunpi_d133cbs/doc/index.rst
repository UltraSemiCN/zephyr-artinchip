.. SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi Technology Development Co., Ltd.
.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: artinchip_kunlunpi_d133cbs

Overview
********

The Kunlun Pi D133CBS is a development board based on the ArtInChip D133CBS
SoC. This Zephyr support is a third-party port maintained by UltraSemi
Technology Development Co., Ltd. It is not an official ArtInChip submission.

The initial board support runs Zephyr from on-chip SRAM and provides a UART0
console. UART0 is also selected as the serial shell device and supports the
interrupt-driven shell backend. The polling console has been validated by
cold-booting the ``hello_world`` sample from SPI NOR through the board's
existing boot chain.

Hardware
********

The currently supported hardware configuration includes:

- ArtInChip D133CBS SoC with a 32-bit T-Head E907-class RISC-V CPU
- legacy memory-mapped CLIC interrupt controller
- machine timer
- 1 MiB physical on-chip SRAM window
- UART0 console at 115200 baud
- GPIO ports A through E with input, output, pull, and interrupt support
- I2C0 controller on PD.0/PD.1
- I2C2 controller on PA.8/PA.9 with GT911 capacitive touch on PA.10 (reset) and PA.11 (interrupt)
- GPAI channel 2 on PA.2 with 4-key resistor ladder (``adc-keys`` input driver)
- PWM2 channel A output on PE.13
- bootloader-configured XSPI PSRAM mapping at ``0x40000000``

Supported Features
==================

.. zephyr:board-supported-hw::

Known Limitations
*****************

- Zephyr uses a provisional 768 KiB SRAM region starting at ``0x30040000``.
  The remaining 256 KiB is reserved for the bootloader until the SRAM
  boundaries have been validated.
- Zephyr controls the UART0 clock gates and reset line. The PLL_INT1 parent
  clock configuration is still inherited from the bootloader. Zephyr configures
  the UART0 PA.0/PA.1 pin multiplexing, pull resistors, and drive strength.
- PSRAM is initialized and trained by the pre-boot firmware. Zephyr validates
  and preserves the inherited XSPI memory mapping without resetting or
  retraining it. The board currently describes an 8 MiB minimum PSRAM window;
  applications must validate the fitted memory before using a larger region.
- D13x QSPI controllers are represented in devicetree but remain disabled on
  this board. QSPI0 is owned by the SPI-NOR boot chain; native pinctrl and
  controller handoff require hardware validation before it can be enabled.
- GPIO ports A through E are enabled, but the board does not yet define named
  LEDs, buttons, or other GPIO consumers. Pin availability and GPIO interrupt
  routing require hardware validation before application use.
- I2C0 provides polling controller-mode transfers at 100 kHz and 400 kHz.
  Connected peripheral addresses and external pull-up resistors require
  hardware validation.
- GT911 touch is on I2C2 at address ``0x5d`` (alternate ``0x14``). Coordinate
  mapping and interrupt polarity require hardware validation against the
  1024x600 MIPI panel.
- The four-channel PWM controller is enabled and PWM2 channel A is routed to
  PE.13, matching the reference board backlight configuration. PWM frequency,
  polarity, and output routing require hardware validation.
- The CPU ISA configuration, timer frequency, and CLIC interrupt behavior
  require additional hardware validation.

Building
********

Build the :zephyr:code-sample:`hello_world` sample with:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: artinchip_kunlunpi_d133cbs
   :goals: build
   :compact:

UART Shell
**********

Build the minimal Zephyr shell sample with the interrupt-driven UART backend:

.. code-block:: console

   west build -p always -b artinchip_kunlunpi_d133cbs \
     samples/subsys/shell/shell_module -d build/d133cbs_shell -- \
     -DCONF_FILE=prj_minimal.conf

Package and flash ``build/d133cbs_shell/zephyr/zephyr.bin`` using the external
AirBurn image tooling described below. Connect UART0 at 115200 baud, 8 data
bits, no parity, and 1 stop bit. Press Enter after boot to display the prompt.
The following commands exercise UART RX and TX:

.. code-block:: console

   uart:~$ demo ping
   pong
   uart:~$ demo board
   artinchip_kunlunpi_d133cbs
   uart:~$ version

The shell uses UART interrupts. If the prompt is printed but input is not
accepted, validate the D13x CLIC interrupt routing and UART0 interrupt number
before debugging shell parsing.

XSPI PSRAM Validation
*********************

The XSPI driver preserves the PSRAM mapping established by the pre-boot
firmware. Build a shell image with the ``devmem`` command enabled:

.. code-block:: console

   west build -p always -b artinchip_kunlunpi_d133cbs \
     samples/subsys/shell/shell_module -d build/d133cbs_xspi_shell -- \
     -DCONFIG_DEVMEM_SHELL=y

After boot, non-destructive reads from the beginning and end of the described
8 MiB window can confirm that the memory mapping remains accessible:

.. code-block:: console

   uart:~$ devmem 40000000 32
   uart:~$ devmem 407ffffc 32

Do not write to PSRAM until the fitted device capacity and bootloader memory
ownership have been validated on the target board.

Flashing
********

No ``west flash`` runner is currently provided. Flashing uses the external
``zephyr-artinchip-tools`` module and the ArtInChip ``luban-lite`` SDK. The
external tooling packages the Zephyr image for the AirBurn/SPI-NOR boot chain;
these packaging tools are not part of the Zephyr repository.

After a successful cold boot, UART0 displays the Zephyr boot banner followed by
output similar to:

.. code-block:: console

   Hello World! artinchip_kunlunpi_d133cbs/d133cbs
