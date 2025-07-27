/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef TDX_DISABLE_PTHREAD

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#include "tdx_pthread.h"
#include <time.h>

/* 
 * In TDX environment, we use the system pthread library directly.
 * This file is kept for compatibility but does not override system functions.
 * If TDX-specific pthread handling is needed in the future, implement
 * wrapper functions with tdx_ prefix.
 */

#endif /* TDX_DISABLE_PTHREAD */