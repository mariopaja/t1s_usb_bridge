# Copyright (c) 2026 Mario Paja
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(t1s_usb_bridge)

# USB device setup shared by the Zephyr USB samples (sample_usbd_init_device())
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

target_sources(app PRIVATE src/main.c)
