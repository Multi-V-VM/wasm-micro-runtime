/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

/* TDX Host-side time operations implementation */

int
tdcall_clock_gettime(unsigned clock_id, void *tp_buf)
{
    return clock_gettime((clockid_t)clock_id, (struct timespec *)tp_buf);
}

int
tdcall_clock_getres(int clock_id, void *res_buf)
{
    return clock_getres((clockid_t)clock_id, (struct timespec *)res_buf);
}

int
tdcall_utimensat(int dirfd, const char *pathname, const void *times_buf, int flags)
{
    return utimensat(dirfd, pathname, (const struct timespec *)times_buf, flags);
}

int
tdcall_futimens(int fd, const void *times_buf)
{
    return futimens(fd, (const struct timespec *)times_buf);
}

int
tdcall_clock_nanosleep(unsigned clock_id, int flags, const void *req_buf, void *rem_buf)
{
    return clock_nanosleep((clockid_t)clock_id, flags, 
                           (const struct timespec *)req_buf,
                           (struct timespec *)rem_buf);
}