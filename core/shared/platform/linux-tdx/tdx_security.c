/*
 * Copyright (C) 2024 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "tdx_security.h"

#include <fcntl.h>
#include <linux/tdx-guest.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static struct {
    bool initialized;
    int tdx_fd;
    tdx_security_info_t info;
} g_tdx_security = { .initialized = false, .tdx_fd = -1 };

static int
open_tdx_guest(void)
{
    int fd = open("/dev/tdx_guest", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        fd = open("/dev/tdx-guest", O_RDWR | O_CLOEXEC);
    return fd;
}

bool
tdx_is_guest(void)
{
    int fd;
    if (g_tdx_security.initialized)
        return true;
    fd = open_tdx_guest();
    if (fd < 0)
        return false;
    close(fd);
    return true;
}

int
tdx_security_init(void)
{
    if (g_tdx_security.initialized)
        return TDX_SUCCESS;
    g_tdx_security.tdx_fd = open_tdx_guest();
    if (g_tdx_security.tdx_fd < 0)
        return TDX_ERROR_NOT_TDX_GUEST;
    memset(&g_tdx_security.info, 0, sizeof(g_tdx_security.info));
    g_tdx_security.info.is_tdx_guest = true;
    g_tdx_security.info.tdx_version = 1;
    g_tdx_security.info.security_features = TDX_SEC_MEMORY_ENCRYPTION | TDX_SEC_ATTESTATION;
    g_tdx_security.info.memory_encryption_enabled = true;
    g_tdx_security.info.attestation_available = true;
    g_tdx_security.initialized = true;
    return TDX_SUCCESS;
}

void
tdx_security_cleanup(void)
{
    if (g_tdx_security.tdx_fd >= 0)
        close(g_tdx_security.tdx_fd);
    memset(&g_tdx_security, 0, sizeof(g_tdx_security));
    g_tdx_security.tdx_fd = -1;
}

int
tdx_get_security_info(tdx_security_info_t *info)
{
    if (!info)
        return TDX_ERROR_INVALID_PARAM;
    if (!g_tdx_security.initialized)
        return TDX_ERROR_NOT_SUPPORTED;
    memcpy(info, &g_tdx_security.info, sizeof(*info));
    return TDX_SUCCESS;
}

int
tdx_generate_report(const uint8_t *report_data, size_t report_data_len,
                    tdx_report_t *report)
{
    struct tdx_report_req request;
    if (!report || (!report_data && report_data_len) || report_data_len > TDX_REPORTDATA_LEN)
        return TDX_ERROR_INVALID_PARAM;
    if (!g_tdx_security.initialized || g_tdx_security.tdx_fd < 0)
        return TDX_ERROR_NOT_SUPPORTED;
    memset(&request, 0, sizeof(request));
    if (report_data_len)
        memcpy(request.reportdata, report_data, report_data_len);
    if (ioctl(g_tdx_security.tdx_fd, TDX_CMD_GET_REPORT0, &request) != 0)
        return TDX_ERROR_ATTESTATION;
    memcpy(report->data, request.tdreport, sizeof(report->data));
    return TDX_SUCCESS;
}

int
tdx_get_quote(const tdx_report_t *report, tdx_quote_t *quote)
{
    if (!report || !quote)
        return TDX_ERROR_INVALID_PARAM;
    memset(quote, 0, sizeof(*quote));
    return TDX_ERROR_NOT_SUPPORTED;
}

int
tdx_verify_quote(const tdx_quote_t *quote, bool *verified)
{
    if (!quote || !verified)
        return TDX_ERROR_INVALID_PARAM;
    *verified = false;
    return TDX_ERROR_NOT_SUPPORTED;
}

int
tdx_protect_memory_region(void *addr, size_t size, uint32_t flags)
{
    (void)flags;
    if (!addr || !size)
        return TDX_ERROR_INVALID_PARAM;
    return g_tdx_security.initialized ? TDX_SUCCESS : TDX_ERROR_NOT_SUPPORTED;
}

int
tdx_unprotect_memory_region(void *addr, size_t size)
{
    if (!addr || !size)
        return TDX_ERROR_INVALID_PARAM;
    return g_tdx_security.initialized ? TDX_SUCCESS : TDX_ERROR_NOT_SUPPORTED;
}

bool
tdx_is_memory_encrypted(void *addr, size_t size)
{
    return addr && size && g_tdx_security.initialized
           && g_tdx_security.info.memory_encryption_enabled;
}

int tdx_verify_secure_boot(void) { return TDX_ERROR_NOT_SUPPORTED; }

int
tdx_get_boot_measurements(uint8_t *measurements, size_t *size)
{
    if (!measurements || !size)
        return TDX_ERROR_INVALID_PARAM;
    *size = 0;
    return TDX_ERROR_NOT_SUPPORTED;
}

int
tdx_derive_key(const uint8_t *label, size_t label_len, uint8_t *key,
               size_t key_len)
{
    if (!label || !label_len || !key || !key_len)
        return TDX_ERROR_INVALID_PARAM;
    memset(key, 0, key_len);
    return TDX_ERROR_NOT_SUPPORTED;
}

int
tdx_seal_data(const uint8_t *data, size_t data_len, uint8_t *sealed_data,
              size_t *sealed_len)
{
    if (!data || !data_len || !sealed_data || !sealed_len)
        return TDX_ERROR_INVALID_PARAM;
    *sealed_len = 0;
    return TDX_ERROR_NOT_SUPPORTED;
}

int
tdx_unseal_data(const uint8_t *sealed_data, size_t sealed_len, uint8_t *data,
                size_t *data_len)
{
    if (!sealed_data || !sealed_len || !data || !data_len)
        return TDX_ERROR_INVALID_PARAM;
    *data_len = 0;
    return TDX_ERROR_NOT_SUPPORTED;
}

int tdx_secure_channel_init(void) { return TDX_ERROR_NOT_SUPPORTED; }

int
tdx_secure_channel_send(const uint8_t *data, size_t len)
{
    return (!data || !len) ? TDX_ERROR_INVALID_PARAM : TDX_ERROR_NOT_SUPPORTED;
}

int
tdx_secure_channel_recv(uint8_t *data, size_t *len)
{
    if (!data || !len)
        return TDX_ERROR_INVALID_PARAM;
    *len = 0;
    return TDX_ERROR_NOT_SUPPORTED;
}

void tdx_secure_channel_cleanup(void) {}
