/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdarg.h>
#include <errno.h>

/* TDX Host-side file operations implementation */

int
tdcall_open(const char *pathname, int flags, bool has_mode, unsigned mode)
{
    if (has_mode) {
        return open(pathname, flags, mode);
    } else {
        return open(pathname, flags);
    }
}

int
tdcall_openat(int dirfd, const char *pathname, int flags, bool has_mode, unsigned mode)
{
    if (has_mode) {
        return openat(dirfd, pathname, flags, mode);
    } else {
        return openat(dirfd, pathname, flags);
    }
}

int
tdcall_close(int fd)
{
    return close(fd);
}

ssize_t
tdcall_read(int fd, void *buf, size_t read_size)
{
    return read(fd, buf, read_size);
}

off_t
tdcall_lseek(int fd, off_t offset, int whence)
{
    return lseek(fd, offset, whence);
}

int
tdcall_ftruncate(int fd, off_t length)
{
    return ftruncate(fd, length);
}

int
tdcall_fsync(int fd)
{
    return fsync(fd);
}

int
tdcall_fdatasync(int fd)
{
    return fdatasync(fd);
}

int
tdcall_isatty(int fd)
{
    return isatty(fd);
}

void
tdcall_fdopendir(int fd, void **p_dirp)
{
    if (p_dirp) {
        *p_dirp = fdopendir(fd);
    }
}

void *
tdcall_readdir(void *dirp)
{
    return readdir((DIR *)dirp);
}

void
tdcall_rewinddir(void *dirp)
{
    rewinddir((DIR *)dirp);
}

void
tdcall_seekdir(void *dirp, long loc)
{
    seekdir((DIR *)dirp, loc);
}

long
tdcall_telldir(void *dirp)
{
    return telldir((DIR *)dirp);
}

int
tdcall_closedir(void *dirp)
{
    return closedir((DIR *)dirp);
}

int
tdcall_stat(const char *pathname, void *buf, unsigned int buf_len)
{
    if (buf_len >= sizeof(struct stat)) {
        return stat(pathname, (struct stat *)buf);
    }
    errno = EINVAL;
    return -1;
}

int
tdcall_fstat(int fd, void *buf, unsigned int buf_len)
{
    if (buf_len >= sizeof(struct stat)) {
        return fstat(fd, (struct stat *)buf);
    }
    errno = EINVAL;
    return -1;
}

int
tdcall_fstatat(int dirfd, const char *pathname, void *buf, unsigned int buf_len, int flags)
{
    if (buf_len >= sizeof(struct stat)) {
        return fstatat(dirfd, pathname, (struct stat *)buf, flags);
    }
    errno = EINVAL;
    return -1;
}

int
tdcall_mkdirat(int dirfd, const char *pathname, unsigned mode)
{
    return mkdirat(dirfd, pathname, mode);
}

int
tdcall_link(const char *oldpath, const char *newpath)
{
    return link(oldpath, newpath);
}

int
tdcall_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
    return linkat(olddirfd, oldpath, newdirfd, newpath, flags);
}

int
tdcall_unlinkat(int dirfd, const char *pathname, int flags)
{
    return unlinkat(dirfd, pathname, flags);
}

ssize_t
tdcall_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz)
{
    return readlinkat(dirfd, pathname, buf, bufsiz);
}

int
tdcall_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
{
    return renameat(olddirfd, oldpath, newdirfd, newpath);
}

int
tdcall_symlinkat(const char *target, int newdirfd, const char *linkpath)
{
    return symlinkat(target, newdirfd, linkpath);
}

int
tdcall_ioctl(int fd, unsigned long request, void *arg, unsigned int arg_len)
{
    return ioctl(fd, request, arg);
}

int
tdcall_fcntl(int fd, int cmd)
{
    return fcntl(fd, cmd);
}

int
tdcall_fcntl_long(int fd, int cmd, long arg)
{
    return fcntl(fd, cmd, arg);
}