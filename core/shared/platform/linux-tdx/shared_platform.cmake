# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set (PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

add_definitions(-DBH_PLATFORM_LINUX_TDX)

# Enable POSIX features for file operations
add_definitions(-D_GNU_SOURCE)
add_definitions(-D_DEFAULT_SOURCE)

include_directories(${PLATFORM_SHARED_DIR})
include_directories(${PLATFORM_SHARED_DIR}/../include)

# TDX SDK directory configuration
if ("$ENV{TDX_SDK}" STREQUAL "")
  set (TDX_SDK_DIR "/opt/intel/tdxsdk")
else()
  set (TDX_SDK_DIR $ENV{TDX_SDK})
endif()

include_directories (${TDX_SDK_DIR}/include)
if (NOT BUILD_UNTRUST_PART EQUAL 1)
  include_directories (${TDX_SDK_DIR}/include/tlibc
                       ${TDX_SDK_DIR}/include/libcxx)
endif ()

if (NOT WAMR_BUILD_THREAD_MGR EQUAL 1)
  add_definitions(-DTDX_DISABLE_PTHREAD)
endif ()

file (GLOB source_all ${PLATFORM_SHARED_DIR}/*.c)

# Remove tdx_thread.c since we're using posix_thread.c
list(REMOVE_ITEM source_all ${PLATFORM_SHARED_DIR}/tdx_thread.c)

# Add TDX security and attestation sources
list(APPEND source_all
    ${PLATFORM_SHARED_DIR}/tdx_security.c
    ${PLATFORM_SHARED_DIR}/tdx_attestation.c
)

if (NOT WAMR_BUILD_LIBC_WASI EQUAL 1)
  add_definitions(-DTDX_DISABLE_WASI)
else()
  list(APPEND source_all
      ${PLATFORM_SHARED_DIR}/../common/posix/posix_file.c
      ${PLATFORM_SHARED_DIR}/../common/posix/posix_clock.c
      ${PLATFORM_SHARED_DIR}/../common/posix/posix_socket.c
      ${PLATFORM_SHARED_DIR}/../common/posix/posix_thread.c
  )
  include (${CMAKE_CURRENT_LIST_DIR}/../common/libc-util/platform_common_libc_util.cmake)
  set(source_all ${source_all} ${PLATFORM_COMMON_LIBC_UTIL_SOURCE})
endif()

file (GLOB source_all_untrusted ${PLATFORM_SHARED_DIR}/untrusted/*.c)

set (PLATFORM_SHARED_SOURCE ${source_all})

set (PLATFORM_SHARED_SOURCE_UNTRUSTED ${source_all_untrusted})