/*
 * Copyright (C) 2024 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/*
 * TDX Guest-Host Interface Stub Implementations
 * 
 * These are placeholder implementations for TDX guest-host communication.
 * In a real TDX environment, these would use the TDCALL instruction to
 * communicate with the TDX module and host.
 */

/* Enable POSIX.1-2008 features for renameat */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

/* Forward declaration for renameat - some systems need this even with _GNU_SOURCE */
extern int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath);

/* Memory management stubs - use standard POSIX functions for now */
void *
tdcall_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    /* In real TDX, this would use TDCALL to request memory from host */
    return mmap(addr, length, prot, flags, fd, offset);
}

int
tdcall_munmap(void *addr, size_t length)
{
    /* In real TDX, this would use TDCALL to release memory to host */
    return munmap(addr, length);
}

int
tdcall_mprotect(void *addr, size_t len, int prot)
{
    /* In real TDX, this would use TDCALL to change memory protection */
    return mprotect(addr, len, prot);
}

/* Time management stubs */
int
tdcall_clock_gettime(unsigned clock_id, void *tp_buf)
{
    /* In real TDX, this would use TDCALL to get time from host */
    return clock_gettime((clockid_t)clock_id, (struct timespec *)tp_buf);
}

int
tdcall_clock_getres(int clock_id, void *res_buf)
{
    /* In real TDX, this would use TDCALL to get clock resolution from host */
    return clock_getres((clockid_t)clock_id, (struct timespec *)res_buf);
}

int
tdcall_clock_nanosleep(unsigned clock_id, int flags, const void *req_buf, void *rem_buf)
{
    /* In real TDX, this would use TDCALL to sleep via host */
    return clock_nanosleep((clockid_t)clock_id, flags, 
                          (const struct timespec *)req_buf,
                          (struct timespec *)rem_buf);
}

int
tdcall_utimensat(int dirfd, const char *pathname, const void *times_buf, int flags)
{
    /* In real TDX, this would use TDCALL to update file times via host */
    return utimensat(dirfd, pathname, (const struct timespec *)times_buf, flags);
}

int
tdcall_futimens(int fd, const void *times_buf)
{
    /* In real TDX, this would use TDCALL to update file times via host */
    return futimens(fd, (const struct timespec *)times_buf);
}

/* Other stubs that may be referenced */
int
tdcall_madvise(void *addr, size_t length, int advice)
{
    return madvise(addr, length, advice);
}

int
tdcall_getentropy(void *buffer, size_t length)
{
    /* In real TDX, this would get entropy from TDX module */
    /* For now, just fill with pseudo-random data */
    for (size_t i = 0; i < length; i++) {
        ((unsigned char*)buffer)[i] = (unsigned char)(rand() & 0xFF);
    }
    return 0;
}

void
tdcall_get_env(const char *name, char *value, unsigned int value_size)
{
    /* In real TDX, environment variables would come from host */
    char *env_val = getenv(name);
    if (env_val) {
        strncpy(value, env_val, value_size - 1);
        value[value_size - 1] = '\0';
    } else {
        value[0] = '\0';
    }
}

int
tdcall_sbrk(intptr_t increment, void **p_old_brk)
{
    /* In real TDX, this would manage heap via host */
    void *old_brk = sbrk(increment);
    if (old_brk == (void *)-1) {
        return -1;
    }
    if (p_old_brk) {
        *p_old_brk = old_brk;
    }
    return 0;
}

/* File operation stubs */
int
tdcall_open(const char *pathname, int flags, bool has_mode, unsigned mode)
{
    if (has_mode) {
        return open(pathname, flags, mode);
    }
    return open(pathname, flags);
}

int
tdcall_openat(int dirfd, const char *pathname, int flags, bool has_mode, unsigned mode)
{
    if (has_mode) {
        return openat(dirfd, pathname, flags, mode);
    }
    return openat(dirfd, pathname, flags);
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

int
tdcall_stat(const char *pathname, void *buf, unsigned int buf_len)
{
    return stat(pathname, (struct stat *)buf);
}

int
tdcall_fstat(int fd, void *buf, unsigned int buf_len)
{
    return fstat(fd, (struct stat *)buf);
}

int
tdcall_fstatat(int dirfd, const char *pathname, void *buf, unsigned int buf_len, int flags)
{
    return fstatat(dirfd, pathname, (struct stat *)buf, flags);
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

/* Directory operations */
void
tdcall_fdopendir(int fd, void **p_dirp)
{
    *p_dirp = fdopendir(fd);
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