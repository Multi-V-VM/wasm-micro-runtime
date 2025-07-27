/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _TDX_FILE_H
#define _TDX_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Use system definitions when available */
#ifndef F_DUPFD
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#endif

#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif

#ifndef O_RDONLY
#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_CREAT 0100
#define O_EXCL 0200
#define O_NOCTTY 0400
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_NDELAY O_NONBLOCK
#define O_SYNC 04010000
#define O_FSYNC O_SYNC
#define O_ASYNC 020000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW 0400000
#define O_CLOEXEC 02000000
#endif

#ifndef AT_FDCWD
#define AT_FDCWD -100
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define AT_SYMLINK_FOLLOW 0x400
#endif

#ifndef O_TMPFILE
#define O_TMPFILE (020000000 | O_DIRECTORY)
#endif

int tdx_open(const char *pathname, int flags, ...);
int tdx_openat(int dirfd, const char *pathname, int flags, ...);
int tdx_close(int fd);
ssize_t tdx_read(int fd, void *buf, size_t count);
ssize_t tdx_write(int fd, const void *buf, size_t count);
off_t tdx_lseek(int fd, off_t offset, int whence);
int tdx_ftruncate(int fd, off_t length);
int tdx_fsync(int fd);
int tdx_fdatasync(int fd);
int tdx_isatty(int fd);
int tdx_fstat(int fd, struct stat *buf);
int tdx_fstatat(int dirfd, const char *pathname, struct stat *buf, int flags);
int tdx_stat(const char *pathname, struct stat *buf);
int tdx_mkdirat(int dirfd, const char *pathname, mode_t mode);
int tdx_link(const char *oldpath, const char *newpath);
int tdx_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags);
int tdx_unlinkat(int dirfd, const char *pathname, int flags);
ssize_t tdx_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
int tdx_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath);
int tdx_symlinkat(const char *target, int newdirfd, const char *linkpath);
int tdx_ioctl(int fd, unsigned long request, ...);
int tdx_fcntl(int fd, int cmd, ...);

/* Directory operations */
DIR *tdx_fdopendir(int fd);
struct dirent *tdx_readdir(DIR *dirp);
void tdx_rewinddir(DIR *dirp);
void tdx_seekdir(DIR *dirp, long loc);
long tdx_telldir(DIR *dirp);
int tdx_closedir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif /* _TDX_FILE_H */