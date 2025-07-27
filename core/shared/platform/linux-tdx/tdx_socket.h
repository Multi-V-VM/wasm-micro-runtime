/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _TDX_SOCKET_H
#define _TDX_SOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* For TDX environment, we use the system socket definitions when available */
#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#else
/* Fallback definitions for non-Linux systems */

/* Socket domains */
#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_INET 2
#define AF_INET6 10

/* Socket types */
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3

/* Socket options */
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_KEEPALIVE 9
#define SO_LINGER 13
#define SO_BROADCAST 6
#define SO_RCVBUF 8
#define SO_SNDBUF 7
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#define SO_ERROR 4
#define SO_TYPE 3

/* Shutdown options */
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

/* Message flags */
#define MSG_OOB 0x01
#define MSG_PEEK 0x02
#define MSG_DONTROUTE 0x04
#define MSG_CTRUNC 0x08
#define MSG_TRUNC 0x20
#define MSG_DONTWAIT 0x40
#define MSG_EOR 0x80
#define MSG_WAITALL 0x100
#define MSG_NOSIGNAL 0x4000

typedef uint32_t socklen_t;
typedef uint16_t sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char __ss_padding[128 - sizeof(sa_family_t)];
};

struct in_addr {
    uint32_t s_addr;
};

struct in6_addr {
    union {
        uint8_t s6_addr[16];
        uint16_t s6_addr16[8];
        uint32_t s6_addr32[4];
    };
};

struct sockaddr_in {
    sa_family_t sin_family;
    uint16_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

struct sockaddr_in6 {
    sa_family_t sin6_family;
    uint16_t sin6_port;
    uint32_t sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t sin6_scope_id;
};

struct linger {
    int l_onoff;
    int l_linger;
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

#endif /* __linux__ */

/* TDX-specific socket functions that wrap the system calls */
int tdx_socket(int domain, int type, int protocol);
int tdx_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int tdx_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
ssize_t tdx_sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t tdx_recvmsg(int sockfd, struct msghdr *msg, int flags);
int tdx_shutdown(int sockfd, int how);
int tdx_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int tdx_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int tdx_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int tdx_listen(int sockfd, int backlog);
int tdx_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int tdx_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t tdx_recv(int sockfd, void *buf, size_t len, int flags);
ssize_t tdx_send(int sockfd, const void *buf, size_t len, int flags);
ssize_t tdx_recvfrom(int sockfd, void *buf, size_t len, int flags,
                     struct sockaddr *src_addr, socklen_t *addrlen);
ssize_t tdx_sendto(int sockfd, const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, socklen_t addrlen);

#ifdef __cplusplus
}
#endif

#endif /* _TDX_SOCKET_H */