/*
 * Copyright (C) 2022 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "tdx_ipfs.h"
#include "tdx_file.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>

static HashMap *ipfs_file_map = NULL;

typedef struct {
    int fd;
    int flags;
    off_t offset;
} ipfs_file_t;

int
ipfs_init()
{
    ipfs_file_map = bh_hash_map_create(32, false, NULL, NULL, NULL, NULL);
    return ipfs_file_map ? 0 : -1;
}

void
ipfs_destroy()
{
    if (ipfs_file_map) {
        bh_hash_map_destroy(ipfs_file_map);
        ipfs_file_map = NULL;
    }
}

int
ipfs_posix_fallocate(int fd, off_t offset, size_t len)
{
    /* TDX: Use standard posix_fallocate */
#ifdef __linux__
    return posix_fallocate(fd, offset, len);
#else
    /* Fallback for systems without posix_fallocate */
    struct stat st;
    if (tdx_fstat(fd, &st) < 0)
        return -1;
    
    if (st.st_size < offset + len) {
        return tdx_ftruncate(fd, offset + len);
    }
    return 0;
#endif
}

size_t
ipfs_read(int fd, const struct iovec *iov, int iovcnt, bool has_offset,
          off_t offset)
{
    ssize_t total = 0;
    
    if (has_offset) {
        if (tdx_lseek(fd, offset, SEEK_SET) == (off_t)-1)
            return -1;
    }
    
    for (int i = 0; i < iovcnt; i++) {
        ssize_t ret = tdx_read(fd, iov[i].iov_base, iov[i].iov_len);
        if (ret < 0)
            return total > 0 ? total : -1;
        if (ret == 0)
            break;
        total += ret;
        if (ret < iov[i].iov_len)
            break;
    }
    
    return total;
}

size_t
ipfs_write(int fd, const struct iovec *iov, int iovcnt, bool has_offset,
           off_t offset)
{
    ssize_t total = 0;
    
    if (has_offset) {
        if (tdx_lseek(fd, offset, SEEK_SET) == (off_t)-1)
            return -1;
    }
    
    for (int i = 0; i < iovcnt; i++) {
        ssize_t ret = write(fd, iov[i].iov_base, iov[i].iov_len);
        if (ret < 0)
            return total > 0 ? total : -1;
        total += ret;
        if (ret < iov[i].iov_len)
            break;
    }
    
    return total;
}

int
ipfs_close(int fd)
{
    ipfs_file_t *file = bh_hash_map_find(ipfs_file_map, (void *)(intptr_t)fd);
    if (file) {
        bh_hash_map_remove(ipfs_file_map, (void *)(intptr_t)fd, NULL, NULL);
        BH_FREE(file);
    }
    return tdx_close(fd);
}

void *
ipfs_fopen(int fd, int flags)
{
    ipfs_file_t *file = BH_MALLOC(sizeof(ipfs_file_t));
    if (!file)
        return NULL;
    
    file->fd = fd;
    file->flags = flags;
    file->offset = 0;
    
    if (!bh_hash_map_insert(ipfs_file_map, (void *)(intptr_t)fd, file)) {
        BH_FREE(file);
        return NULL;
    }
    
    return file;
}

int
ipfs_fflush(int fd)
{
    return tdx_fsync(fd);
}

off_t
ipfs_lseek(int fd, off_t offset, int whence)
{
    return tdx_lseek(fd, offset, whence);
}

int
ipfs_ftruncate(int fd, off_t length)
{
    return tdx_ftruncate(fd, length);
}