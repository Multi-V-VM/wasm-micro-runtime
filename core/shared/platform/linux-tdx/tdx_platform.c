/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include "tdx_security.h"
#include "tdx_attestation.h"

/* Memory management operations - declare before use */
extern void *tdcall_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
extern int tdcall_munmap(void *addr, size_t length);
extern int tdcall_mprotect(void *addr, size_t len, int prot);
extern int tdcall_madvise(void *addr, size_t length, int advice);
extern int tdcall_getentropy(void *buffer, size_t length);
extern void tdcall_get_env(const char *name, char *value, unsigned int value_size);
extern int tdcall_sbrk(intptr_t increment, void **p_old_brk);

#ifndef TDX_DISABLE_PTHREAD

/* Define thread local storage based on compiler */
#if defined(__GNUC__) || defined(__clang__)
    #define os_thread_local_attribute __thread
#else
    #define os_thread_local_attribute
#endif

/* Thread signal initialization flag - currently unused in TDX */
/* static os_thread_local_attribute bool thread_signal_inited = false; */

#endif

int
bh_platform_init()
{
    int ret;
    
    /* Initialize TDX security subsystem */
    ret = tdx_security_init();
    if (ret != TDX_SUCCESS) {
        /* Not running in TDX or security init failed */
        /* Continue without TDX-specific features */
    }
    
    /* Initialize attestation if TDX is available */
    if (tdx_is_guest()) {
        ret = tdx_attestation_init(NULL);
        if (ret != TDX_ATTEST_SUCCESS) {
            /* Attestation not available */
        }
    }
    
    return 0;
}

void
bh_platform_destroy()
{
    /* Cleanup attestation subsystem */
    tdx_attestation_cleanup();
    
    /* Cleanup TDX security subsystem */
    tdx_security_cleanup();
}

int
os_printf(const char *format, ...)
{
    va_list ap;
    char buffer[128];
    int ret;

    va_start(ap, format);
    ret = vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);

    if (ret > 0) {
        /* Output to TDX debug console or logging mechanism */
        /* This would be implemented based on TDX capabilities */
    }

    return ret;
}

int
os_vprintf(const char *format, va_list ap)
{
    char buffer[128];
    int ret;

    ret = vsnprintf(buffer, sizeof(buffer), format, ap);

    if (ret > 0) {
        /* Output to TDX debug console or logging mechanism */
        /* This would be implemented based on TDX capabilities */
    }

    return ret;
}

void *
os_malloc(unsigned size)
{
    return malloc(size);
}

void *
os_realloc(void *ptr, unsigned size)
{
    return realloc(ptr, size);
}

void
os_free(void *ptr)
{
    free(ptr);
}

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file_handle)
{
    return tdcall_mmap(hint, size, prot, flags, file_handle, 0);
}

void
os_munmap(void *addr, size_t size)
{
    tdcall_munmap(addr, size);
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    return tdcall_mprotect(addr, size, prot);
}

void
os_dcache_flush()
{
    /* TDX typically handles cache coherency automatically */
}

void
os_icache_flush(void *start, size_t len)
{
    /* TDX typically handles cache coherency automatically */
}

int
os_dumps_proc_mem_info(char *out, unsigned int size)
{
    int out_size = snprintf(out, size, "TDX guest memory info not available\n");
    if (out_size >= size) {
        out[size - 1] = '\0';
    }
    return 0;
}

/* Thread stack boundary and JIT write protection are now handled by posix_thread.c */