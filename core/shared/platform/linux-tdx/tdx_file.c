/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#include "tdx_file.h"
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>

/* TDX guest-host interface calls declarations */
extern int tdcall_open(const char *pathname, int flags, bool has_mode, unsigned mode);
extern int tdcall_openat(int dirfd, const char *pathname, int flags, bool has_mode, unsigned mode);
extern int tdcall_close(int fd);
extern ssize_t tdcall_read(int fd, void *buf, size_t read_size);
extern off_t tdcall_lseek(int fd, off_t offset, int whence);
extern int tdcall_ftruncate(int fd, off_t length);
extern int tdcall_fsync(int fd);
extern int tdcall_fdatasync(int fd);
extern int tdcall_isatty(int fd);
extern int tdcall_stat(const char *pathname, void *buf, unsigned int buf_len);
extern int tdcall_fstat(int fd, void *buf, unsigned int buf_len);
extern int tdcall_fstatat(int dirfd, const char *pathname, void *buf, unsigned int buf_len, int flags);
extern int tdcall_mkdirat(int dirfd, const char *pathname, unsigned mode);
extern int tdcall_link(const char *oldpath, const char *newpath);
extern int tdcall_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags);
extern int tdcall_unlinkat(int dirfd, const char *pathname, int flags);
extern ssize_t tdcall_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
extern int tdcall_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath);
extern int tdcall_symlinkat(const char *target, int newdirfd, const char *linkpath);
extern int tdcall_ioctl(int fd, unsigned long request, void *arg, unsigned int arg_len);
extern int tdcall_fcntl(int fd, int cmd);
extern int tdcall_fcntl_long(int fd, int cmd, long arg);

/* Directory operations */
extern void tdcall_fdopendir(int fd, void **p_dirp);
extern void *tdcall_readdir(void *dirp);
extern void tdcall_rewinddir(void *dirp);
extern void tdcall_seekdir(void *dirp, long loc);
extern long tdcall_telldir(void *dirp);
extern int tdcall_closedir(void *dirp);

int
tdx_open(const char *pathname, int flags, ...)
{
    va_list ap;
    unsigned mode = 0;
    bool has_mode = false;

    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
        va_start(ap, flags);
        mode = va_arg(ap, unsigned);
        va_end(ap);
        has_mode = true;
    }

    return tdcall_open(pathname, flags, has_mode, mode);
}

int
tdx_openat(int dirfd, const char *pathname, int flags, ...)
{
    va_list ap;
    unsigned mode = 0;
    bool has_mode = false;

    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
        va_start(ap, flags);
        mode = va_arg(ap, unsigned);
        va_end(ap);
        has_mode = true;
    }

    return tdcall_openat(dirfd, pathname, flags, has_mode, mode);
}

int
tdx_close(int fd)
{
    return tdcall_close(fd);
}

ssize_t
tdx_read(int fd, void *buf, size_t count)
{
    return tdcall_read(fd, buf, count);
}

off_t
tdx_lseek(int fd, off_t offset, int whence)
{
    return tdcall_lseek(fd, offset, whence);
}

int
tdx_ftruncate(int fd, off_t length)
{
    return tdcall_ftruncate(fd, length);
}

int
tdx_fsync(int fd)
{
    return tdcall_fsync(fd);
}

int
tdx_fdatasync(int fd)
{
    return tdcall_fdatasync(fd);
}

int
tdx_isatty(int fd)
{
    return tdcall_isatty(fd);
}

int
tdx_fstat(int fd, struct stat *buf)
{
    return tdcall_fstat(fd, buf, sizeof(struct stat));
}

int
tdx_fstatat(int dirfd, const char *pathname, struct stat *buf, int flags)
{
    return tdcall_fstatat(dirfd, pathname, buf, sizeof(struct stat), flags);
}

int
tdx_stat(const char *pathname, struct stat *buf)
{
    return tdcall_stat(pathname, buf, sizeof(struct stat));
}

int
tdx_mkdirat(int dirfd, const char *pathname, mode_t mode)
{
    return tdcall_mkdirat(dirfd, pathname, mode);
}

int
tdx_link(const char *oldpath, const char *newpath)
{
    return tdcall_link(oldpath, newpath);
}

int
tdx_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
    return tdcall_linkat(olddirfd, oldpath, newdirfd, newpath, flags);
}

int
tdx_unlinkat(int dirfd, const char *pathname, int flags)
{
    return tdcall_unlinkat(dirfd, pathname, flags);
}

ssize_t
tdx_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz)
{
    return tdcall_readlinkat(dirfd, pathname, buf, bufsiz);
}

int
tdx_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
{
    return tdcall_renameat(olddirfd, oldpath, newdirfd, newpath);
}

int
tdx_symlinkat(const char *target, int newdirfd, const char *linkpath)
{
    return tdcall_symlinkat(target, newdirfd, linkpath);
}

int
tdx_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;
    
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    
    return tdcall_ioctl(fd, request, arg, 0);
}

int
tdx_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    long arg = 0;

    if (cmd == F_SETFL || cmd == F_SETFD || cmd == F_DUPFD) {
        va_start(ap, cmd);
        arg = va_arg(ap, long);
        va_end(ap);
        return tdcall_fcntl_long(fd, cmd, arg);
    }

    return tdcall_fcntl(fd, cmd);
}

/* Directory operations */
DIR *
tdx_fdopendir(int fd)
{
    DIR *dirp = NULL;
    tdcall_fdopendir(fd, (void**)&dirp);
    return dirp;
}

struct dirent *
tdx_readdir(DIR *dirp)
{
    return (struct dirent *)tdcall_readdir(dirp);
}

void
tdx_rewinddir(DIR *dirp)
{
    tdcall_rewinddir(dirp);
}

void
tdx_seekdir(DIR *dirp, long loc)
{
    tdcall_seekdir(dirp, loc);
}

long
tdx_telldir(DIR *dirp)
{
    return tdcall_telldir(dirp);
}

int
tdx_closedir(DIR *dirp)
{
    return tdcall_closedir(dirp);
}