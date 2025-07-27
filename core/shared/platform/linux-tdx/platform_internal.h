/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

#include <inttypes.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include "tdx_file.h"
#include "tdx_pthread.h"
#include "tdx_time.h"
#include "tdx_socket.h"
#include "tdx_signal.h"
#include "tdx_security.h"
#include "tdx_attestation.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BH_PLATFORM_LINUX_TDX
#define BH_PLATFORM_LINUX_TDX
#endif

#define _STACK_SIZE_ADJUSTMENT (32 * 1024)

/* Stack size of applet threads's native part.  */
#define BH_APPLET_PRESERVED_STACK_SIZE (8 * 1024 + _STACK_SIZE_ADJUSTMENT)

/* Default thread priority */
#define BH_THREAD_DEFAULT_PRIORITY 0

typedef pthread_t korp_thread;
typedef pthread_t korp_tid;
typedef pthread_mutex_t korp_mutex;
typedef pthread_cond_t korp_cond;
typedef pthread_rwlock_t korp_rwlock;
typedef sem_t korp_sem;

#ifndef TDX_DISABLE_PTHREAD
#define OS_THREAD_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#endif

typedef int (*os_print_function_t)(const char *message);
void
os_set_print_function(os_print_function_t pf);

char *
strcpy(char *dest, const char *src);

#define os_memory_order_acquire __ATOMIC_ACQUIRE
#define os_memory_order_release __ATOMIC_RELEASE
#define os_memory_order_seq_cst __ATOMIC_SEQ_CST
#define os_atomic_thread_fence __atomic_thread_fence

typedef int os_file_handle;
typedef DIR *os_dir_stream;
typedef int os_raw_file_handle;

static inline os_file_handle
os_get_invalid_handle()
{
    return -1;
}

#ifdef __cplusplus
}
#endif

#endif /* end of _PLATFORM_INTERNAL_H */