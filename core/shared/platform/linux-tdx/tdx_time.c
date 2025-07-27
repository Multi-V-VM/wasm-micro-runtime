/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "tdx_time.h"

/* TDX guest-host interface calls declarations */
extern int tdcall_clock_gettime(unsigned clock_id, void *tp_buf);
extern int tdcall_clock_getres(int clock_id, void *res_buf);
extern int tdcall_clock_nanosleep(unsigned clock_id, int flags, const void *req_buf, void *rem_buf);
extern int tdcall_utimensat(int dirfd, const char *pathname, const void *times_buf, int flags);
extern int tdcall_futimens(int fd, const void *times_buf);

uint64_t
os_time_thread_cputime_us(void)
{
    struct timespec ts;
    if (tdx_clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return 0;
    }

    return ((uint64_t)ts.tv_sec) * 1000 * 1000 + ((uint64_t)ts.tv_nsec) / 1000;
}

int
tdx_clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    return tdcall_clock_gettime(clk_id, tp);
}

int
tdx_clock_getres(clockid_t clk_id, struct timespec *res)
{
    return tdcall_clock_getres(clk_id, res);
}

int
tdx_clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *request,
                    struct timespec *remain)
{
    return tdcall_clock_nanosleep(clock_id, flags, request, remain);
}

int
tdx_nanosleep(const struct timespec *req, struct timespec *rem)
{
    return tdx_clock_nanosleep(CLOCK_REALTIME, 0, req, rem);
}

int
tdx_usleep(useconds_t usec)
{
    struct timespec ts;
    ts.tv_sec = usec / 1000000;
    ts.tv_nsec = (usec % 1000000) * 1000;
    return tdx_nanosleep(&ts, NULL);
}

int
tdx_utimensat(int dirfd, const char *pathname, const struct timespec times[2],
              int flags)
{
    return tdcall_utimensat(dirfd, pathname, times, flags);
}

int
tdx_futimens(int fd, const struct timespec times[2])
{
    return tdcall_futimens(fd, times);
}

uint64_t
os_time_get_boot_microsecond()
{
#ifndef TDX_DISABLE_WASI
    struct timespec ts;
    if (tdx_clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec) * 1000 * 1000 + ((uint64_t)ts.tv_nsec) / 1000;
#else
    return 0;
#endif
}