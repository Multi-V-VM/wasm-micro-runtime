/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#include "tdx_socket.h"

/* TDX guest-host interface calls declarations */
extern int tdcall_socket(int domain, int type, int protocol);
extern int tdcall_getsockopt(int sockfd, int level, int optname, void *val_buf, 
                            unsigned int val_buf_size, void *len_buf);
extern int tdcall_setsockopt(int sockfd, int level, int optname, void *optval, unsigned int optlen);
extern int tdcall_sendmsg(int sockfd, void *msg_buf, unsigned int msg_buf_size, int flags);
extern int tdcall_recvmsg(int sockfd, void *msg_buf, unsigned int msg_buf_size, int flags);
extern int tdcall_shutdown(int sockfd, int how);
extern int tdcall_bind(int sockfd, const void *addr, unsigned int addrlen);
extern int tdcall_getsockname(int sockfd, void *addr, unsigned int addr_size, void *addrlen);
extern int tdcall_getpeername(int sockfd, void *addr, unsigned int addr_size, void *addrlen);
extern int tdcall_listen(int sockfd, int backlog);
extern int tdcall_accept(int sockfd, void *addr, unsigned int addr_size, void *addrlen);
extern int tdcall_connect(int sockfd, void *addr, unsigned int addrlen);
extern int tdcall_recv(int sockfd, void *buf, size_t len, int flags);
extern int tdcall_send(int sockfd, const void *buf, size_t len, int flags);

int
tdx_socket(int domain, int type, int protocol)
{
    return tdcall_socket(domain, type, protocol);
}

int
tdx_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    return tdcall_getsockopt(sockfd, level, optname, optval, 
                            optlen ? *optlen : 0, optlen);
}

int
tdx_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    return tdcall_setsockopt(sockfd, level, optname, (void *)optval, optlen);
}

ssize_t
tdx_sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    return tdcall_sendmsg(sockfd, (void *)msg, sizeof(struct msghdr), flags);
}

ssize_t
tdx_recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    return tdcall_recvmsg(sockfd, msg, sizeof(struct msghdr), flags);
}

int
tdx_shutdown(int sockfd, int how)
{
    return tdcall_shutdown(sockfd, how);
}

int
tdx_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return tdcall_bind(sockfd, addr, addrlen);
}

int
tdx_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return tdcall_getsockname(sockfd, addr, addrlen ? *addrlen : 0, addrlen);
}

int
tdx_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return tdcall_getpeername(sockfd, addr, addrlen ? *addrlen : 0, addrlen);
}

int
tdx_listen(int sockfd, int backlog)
{
    return tdcall_listen(sockfd, backlog);
}

int
tdx_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return tdcall_accept(sockfd, addr, addrlen ? *addrlen : 0, addrlen);
}

int
tdx_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return tdcall_connect(sockfd, (void *)addr, addrlen);
}

ssize_t
tdx_recv(int sockfd, void *buf, size_t len, int flags)
{
    return tdcall_recv(sockfd, buf, len, flags);
}

ssize_t
tdx_send(int sockfd, const void *buf, size_t len, int flags)
{
    return tdcall_send(sockfd, buf, len, flags);
}

ssize_t
tdx_recvfrom(int sockfd, void *buf, size_t len, int flags,
             struct sockaddr *src_addr, socklen_t *addrlen)
{
    /* For now, implement using recv and getpeername if src_addr is needed */
    ssize_t ret = tdx_recv(sockfd, buf, len, flags);
    if (ret >= 0 && src_addr && addrlen) {
        tdx_getpeername(sockfd, src_addr, addrlen);
    }
    return ret;
}

ssize_t
tdx_sendto(int sockfd, const void *buf, size_t len, int flags,
           const struct sockaddr *dest_addr, socklen_t addrlen)
{
    /* For connection-less sockets, may need to temporarily connect */
    if (dest_addr) {
        /* This is a simplified implementation */
        tdx_connect(sockfd, dest_addr, addrlen);
    }
    return tdx_send(sockfd, buf, len, flags);
}