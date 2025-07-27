/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

/* TDX Host-side socket operations implementation */

int
tdcall_socket(int domain, int type, int protocol)
{
    return socket(domain, type, protocol);
}

int
tdcall_getsockopt(int sockfd, int level, int optname, void *val_buf,
                  unsigned int val_buf_size, void *len_buf)
{
    socklen_t *optlen = (socklen_t *)len_buf;
    return getsockopt(sockfd, level, optname, val_buf, optlen);
}

int
tdcall_setsockopt(int sockfd, int level, int optname, void *optval, unsigned int optlen)
{
    return setsockopt(sockfd, level, optname, optval, (socklen_t)optlen);
}

int
tdcall_sendmsg(int sockfd, void *msg_buf, unsigned int msg_buf_size, int flags)
{
    return sendmsg(sockfd, (const struct msghdr *)msg_buf, flags);
}

int
tdcall_recvmsg(int sockfd, void *msg_buf, unsigned int msg_buf_size, int flags)
{
    return recvmsg(sockfd, (struct msghdr *)msg_buf, flags);
}

int
tdcall_shutdown(int sockfd, int how)
{
    return shutdown(sockfd, how);
}

int
tdcall_bind(int sockfd, const void *addr, unsigned int addrlen)
{
    return bind(sockfd, (const struct sockaddr *)addr, (socklen_t)addrlen);
}

int
tdcall_getsockname(int sockfd, void *addr, unsigned int addr_size, void *addrlen)
{
    socklen_t *len = (socklen_t *)addrlen;
    return getsockname(sockfd, (struct sockaddr *)addr, len);
}

int
tdcall_getpeername(int sockfd, void *addr, unsigned int addr_size, void *addrlen)
{
    socklen_t *len = (socklen_t *)addrlen;
    return getpeername(sockfd, (struct sockaddr *)addr, len);
}

int
tdcall_listen(int sockfd, int backlog)
{
    return listen(sockfd, backlog);
}

int
tdcall_accept(int sockfd, void *addr, unsigned int addr_size, void *addrlen)
{
    socklen_t *len = (socklen_t *)addrlen;
    return accept(sockfd, (struct sockaddr *)addr, len);
}

int
tdcall_connect(int sockfd, void *addr, unsigned int addrlen)
{
    return connect(sockfd, (const struct sockaddr *)addr, (socklen_t)addrlen);
}

int
tdcall_recv(int sockfd, void *buf, size_t len, int flags)
{
    return recv(sockfd, buf, len, flags);
}

int
tdcall_send(int sockfd, const void *buf, size_t len, int flags)
{
    return send(sockfd, buf, len, flags);
}