# SPDX-FileCopyrightText: Copyright (c) 2026 UltraSemi
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(artinchip_d21x_reboot)

target_include_directories(app PRIVATE ${ZEPHYR_BASE}/soc/artinchip/d21x)
target_sources(app PRIVATE src/main.c)
