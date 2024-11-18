#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)

# Protobuf c source files generation
#set(NANOPB_OPTIONS "-I${CMAKE_SOURCE_DIR}")
#nanopb_generate_cpp(proto_sources proto_headers RELPATH .
#  protocol/renforce.proto
#)
list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)
zephyr_nanopb_sources(app protocol/protocol.proto)

# Make protobuf generated header files (in build dir) available to source files
#zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Include needed source files for build
FILE(GLOB app_sources src/*c src/bluetooth/*.c src/nanocobs/*.c src/calendar/*.c)

# Create app from source files AND protobuf files
#target_sources(app PRIVATE ${proto_sources} ${app_sources})
target_sources(app PRIVATE ${app_sources})

