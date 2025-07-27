/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "tdx_signal.h"

/* TDX guest-host interface calls declarations */
extern int tdcall_raise(int sig);

int
tdx_raise(int sig)
{
    return tdcall_raise(sig);
}

sighandler_t
tdx_signal(int signum, sighandler_t handler)
{
    /* In TDX environment, signal handling is limited */
    /* This is a simplified implementation */
    return SIG_DFL;
}

int
tdx_sigemptyset(sigset_t *set)
{
    return sigemptyset(set);
}

int
tdx_sigfillset(sigset_t *set)
{
    return sigfillset(set);
}

int
tdx_sigaddset(sigset_t *set, int signum)
{
    return sigaddset(set, signum);
}

int
tdx_sigdelset(sigset_t *set, int signum)
{
    return sigdelset(set, signum);
}

int
tdx_sigismember(const sigset_t *set, int signum)
{
    return sigismember(set, signum);
}