/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _TDX_TIME_H
#define _TDX_TIME_H

#include <stdint.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Use system time definitions when available */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
#define CLOCK_MONOTONIC_RAW 4
#define CLOCK_REALTIME_COARSE 5
#define CLOCK_MONOTONIC_COARSE 6
#define CLOCK_BOOTTIME 7
#endif

#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 0x01
#endif

/* TDX-specific time functions */
int tdx_clock_gettime(clockid_t clk_id, struct timespec *tp);
int tdx_clock_getres(clockid_t clk_id, struct timespec *res);
int tdx_clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *request,
                        struct timespec *remain);
int tdx_nanosleep(const struct timespec *req, struct timespec *rem);
int tdx_usleep(useconds_t usec);
int tdx_utimensat(int dirfd, const char *pathname, const struct timespec times[2],
                  int flags);
int tdx_futimens(int fd, const struct timespec times[2]);

#ifdef __cplusplus
}
#endif

#endif /* _TDX_TIME_H */