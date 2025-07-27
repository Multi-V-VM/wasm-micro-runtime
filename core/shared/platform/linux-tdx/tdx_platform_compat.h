/*
 * Copyright (C) 2024 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _TDX_PLATFORM_COMPAT_H
#define _TDX_PLATFORM_COMPAT_H

/* Enable required POSIX features */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

/* Include necessary system headers for WASI implementation */
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/uio.h>

/* Define any missing constants if needed */
#ifndef FIONREAD
#include <termios.h>
#endif

#endif /* _TDX_PLATFORM_COMPAT_H */