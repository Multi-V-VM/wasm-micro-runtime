/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _TDX_SIGNAL_H
#define _TDX_SIGNAL_H

#include <stdint.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TDX-specific signal handling functions */
/* By default, we use the system signal definitions */

/* TDX signal functions */
int tdx_raise(int sig);
sighandler_t tdx_signal(int signum, sighandler_t handler);

/* TDX signal set operations */
int tdx_sigemptyset(sigset_t *set);
int tdx_sigfillset(sigset_t *set);
int tdx_sigaddset(sigset_t *set, int signum);
int tdx_sigdelset(sigset_t *set, int signum);
int tdx_sigismember(const sigset_t *set, int signum);

#ifdef __cplusplus
}
#endif

#endif /* _TDX_SIGNAL_H */