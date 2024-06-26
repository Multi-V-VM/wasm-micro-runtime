# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required (VERSION 2.8...3.16)

project(socket_wasi_ext)

add_library(${PROJECT_NAME} STATIC ${CMAKE_CURRENT_LIST_DIR}/src/wasi/wasi_socket_ext.c)
set(CMAKE_C_FLAGS "-fPIC")
if(WIN32)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/inc/ D:/wasi-sdk/share/wasi-sysroot/include/)
else()
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/inc/)
endif()
