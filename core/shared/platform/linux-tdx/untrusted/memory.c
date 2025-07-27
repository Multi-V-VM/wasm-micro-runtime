/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* TDX Host-side memory operations implementation */

void *
tdcall_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    return mmap(addr, length, prot, flags, fd, offset);
}

int
tdcall_munmap(void *addr, size_t length)
{
    return munmap(addr, length);
}

int
tdcall_mprotect(void *addr, size_t len, int prot)
{
    return mprotect(addr, len, prot);
}

int
tdcall_madvise(void *addr, size_t length, int advice)
{
    return madvise(addr, length, advice);
}

int
tdcall_getentropy(void *buffer, size_t length)
{
#ifdef __linux__
    return getentropy(buffer, length);
#else
    /* Fallback implementation for systems without getentropy */
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    
    size_t read_bytes = fread(buffer, 1, length, f);
    fclose(f);
    
    return (read_bytes == length) ? 0 : -1;
#endif
}

void
tdcall_get_env(const char *name, char *value, unsigned int value_size)
{
    char *env_value = getenv(name);
    if (env_value && value && value_size > 0) {
        strncpy(value, env_value, value_size - 1);
        value[value_size - 1] = '\0';
    } else if (value && value_size > 0) {
        value[0] = '\0';
    }
}

int
tdcall_sbrk(intptr_t increment, void **p_old_brk)
{
    void *old_brk = sbrk(0);
    void *new_brk = sbrk(increment);
    
    if (new_brk == (void *)-1) {
        return -1;
    }
    
    if (p_old_brk) {
        *p_old_brk = old_brk;
    }
    
    return 0;
}