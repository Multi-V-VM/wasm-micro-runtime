/*
 * Copyright (C) 2022 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _LIBC_WASI_TDX_PFS_H
#define _LIBC_WASI_TDX_PFS_H

#include "bh_hashmap.h"
#include <sys/uio.h>

#ifdef __cplusplus
extern "C" {
#endif

int
ipfs_init();

void
ipfs_destroy();

int
ipfs_posix_fallocate(int fd, off_t offset, size_t len);

size_t
ipfs_read(int fd, const struct iovec *iov, int iovcnt, bool has_offset,
          off_t offset);

size_t
ipfs_write(int fd, const struct iovec *iov, int iovcnt, bool has_offset,
           off_t offset);

int
ipfs_close(int fd);

void *
ipfs_fopen(int fd, int flags);

int
ipfs_fflush(int fd);

off_t
ipfs_lseek(int fd, off_t offset, int whence);

int
ipfs_ftruncate(int fd, off_t length);

#ifdef __cplusplus
}
#endif

#endif /* _LIBC_WASI_TDX_PFS_H */