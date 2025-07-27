/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _TDX_PTHREAD_H
#define _TDX_PTHREAD_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TDX_DISABLE_PTHREAD

/* TDX pthread wrapper functions */
/* These are provided for TDX-specific implementations if needed */
/* By default, we use the system pthread library */

#endif /* TDX_DISABLE_PTHREAD */

#ifdef __cplusplus
}
#endif

#endif /* _TDX_PTHREAD_H */