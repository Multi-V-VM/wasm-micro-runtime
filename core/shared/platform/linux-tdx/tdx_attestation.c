/*
 * Copyright (C) 2024 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "tdx_attestation.h"
#include "tdx_security.h"

#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TDX_QUOTE_MAX_SIZE (1024U * 1024U)
#define TSM_REPORT_ROOT "/sys/kernel/config/tsm/report"

static struct {
    bool initialized;
    tdx_attestation_config_t config;
} g_attestation;

static atomic_ulong g_report_sequence;

static int
write_all(int fd, const uint8_t *data, size_t size)
{
    while (size) {
        ssize_t written = write(fd, data, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return 0;
        data += written;
        size -= (size_t)written;
    }
    return 1;
}

static int
read_tdx_quote(const uint8_t report_data[64], uint8_t **quote,
               size_t *quote_size)
{
    char directory[256], path[320], provider[32] = { 0 };
    uint8_t *buffer = NULL;
    size_t used = 0, capacity = 4096;
    int fd = -1, result = TDX_ATTEST_ERROR_QUOTE_FAILURE;
    unsigned long sequence = atomic_fetch_add_explicit(
        &g_report_sequence, 1, memory_order_relaxed);

    if (snprintf(directory, sizeof(directory), TSM_REPORT_ROOT "/wamr-%ld-%lu",
                 (long)getpid(), sequence)
        >= (int)sizeof(directory)
        || mkdir(directory, 0700) != 0)
        return result;

    if (snprintf(path, sizeof(path), "%s/provider", directory)
            >= (int)sizeof(path)
        || (fd = open(path, O_RDONLY | O_CLOEXEC)) < 0
        || read(fd, provider, sizeof(provider) - 1) <= 0
        || strncmp(provider, "tdx_guest", strlen("tdx_guest")) != 0)
        goto cleanup;
    close(fd);
    fd = -1;

    if (snprintf(path, sizeof(path), "%s/inblob", directory)
            >= (int)sizeof(path)
        || (fd = open(path, O_WRONLY | O_CLOEXEC)) < 0
        || !write_all(fd, report_data, 64))
        goto cleanup;
    close(fd);
    fd = -1;

    if (snprintf(path, sizeof(path), "%s/outblob", directory)
            >= (int)sizeof(path)
        || (fd = open(path, O_RDONLY | O_CLOEXEC)) < 0
        || !(buffer = malloc(capacity)))
        goto cleanup;
    for (;;) {
        ssize_t count;
        if (used == capacity) {
            uint8_t *larger;
            if (capacity == TDX_QUOTE_MAX_SIZE)
                goto cleanup;
            capacity *= 2;
            if (capacity > TDX_QUOTE_MAX_SIZE)
                capacity = TDX_QUOTE_MAX_SIZE;
            if (!(larger = realloc(buffer, capacity)))
                goto cleanup;
            buffer = larger;
        }
        count = read(fd, buffer + used, capacity - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            goto cleanup;
        if (count == 0)
            break;
        used += (size_t)count;
    }
    if (!used)
        goto cleanup;
    *quote = buffer;
    *quote_size = used;
    buffer = NULL;
    result = TDX_ATTEST_SUCCESS;

cleanup:
    if (fd >= 0)
        close(fd);
    free(buffer);
    rmdir(directory);
    return result;
}

static int
sha384_hash(const uint8_t *data, size_t len, uint8_t hash[48])
{
    EVP_MD_CTX *ctx;
    unsigned int hash_len = 0;
    int ok;

    if ((!data && len) || !hash)
        return 0;
    ctx = EVP_MD_CTX_new();
    if (!ctx)
        return 0;
    ok = EVP_DigestInit_ex(ctx, EVP_sha384(), NULL) == 1
         && EVP_DigestUpdate(ctx, data, len) == 1
         && EVP_DigestFinal_ex(ctx, hash, &hash_len) == 1
         && hash_len == 48;
    EVP_MD_CTX_free(ctx);
    return ok;
}

int
tdx_attestation_init(const tdx_attestation_config_t *config)
{
    if (g_attestation.initialized)
        return TDX_ATTEST_SUCCESS;
    if (tdx_security_init() != TDX_SUCCESS)
        return TDX_ATTEST_ERROR_NOT_SUPPORTED;
    memset(&g_attestation.config, 0, sizeof(g_attestation.config));
    if (config)
        memcpy(&g_attestation.config, config, sizeof(*config));
    g_attestation.initialized = true;
    return TDX_ATTEST_SUCCESS;
}

void
tdx_attestation_cleanup(void)
{
    tdx_security_cleanup();
    memset(&g_attestation, 0, sizeof(g_attestation));
}

int
tdx_attestation_generate_report(const uint8_t *user_data, size_t user_data_len,
                                tdx_report_full_t *report)
{
    tdx_report_t raw_report;
    uint8_t report_data[64] = { 0 };

    _Static_assert(sizeof(tdx_report_t) == sizeof(tdx_report_full_t),
                   "TDREPORT ABI must be exactly 1024 bytes");
    if (!report || (!user_data && user_data_len))
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    if (!g_attestation.initialized)
        return TDX_ATTEST_ERROR_NOT_INITIALIZED;
    if (user_data_len <= sizeof(report_data)) {
        if (user_data_len)
            memcpy(report_data, user_data, user_data_len);
    }
    else if (!sha384_hash(user_data, user_data_len, report_data)) {
        return TDX_ATTEST_ERROR_REPORT_FAILURE;
    }
    if (tdx_generate_report(report_data, sizeof(report_data), &raw_report) != TDX_SUCCESS)
        return TDX_ATTEST_ERROR_REPORT_FAILURE;
    memcpy(report, raw_report.data, sizeof(*report));
    return TDX_ATTEST_SUCCESS;
}

int
tdx_attestation_get_quote(const tdx_report_full_t *report, uint8_t **quote,
                          size_t *quote_size)
{
    if (!report || !quote || !quote_size)
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    if (!g_attestation.initialized)
        return TDX_ATTEST_ERROR_NOT_INITIALIZED;
    *quote = NULL;
    *quote_size = 0;
    return read_tdx_quote(report->report_mac_struct.report_data, quote,
                          quote_size);
}

int
tdx_attestation_get_collateral(const uint8_t *quote, size_t quote_size,
                               uint8_t **collateral, size_t *collateral_size)
{
    if (!quote || !quote_size || !collateral || !collateral_size)
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    *collateral = NULL;
    *collateral_size = 0;
    return TDX_ATTEST_ERROR_NOT_SUPPORTED;
}

int
tdx_attestation_verify_quote(const uint8_t *quote, size_t quote_size,
                             const uint8_t *collateral, size_t collateral_size,
                             uint32_t *tcb_status)
{
    if (!quote || !quote_size || !tcb_status)
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    (void)collateral;
    (void)collateral_size;
    *tcb_status = TDX_TCB_STATUS_REVOKED;
    return TDX_ATTEST_ERROR_NOT_SUPPORTED;
}

int
tdx_attestation_generate_evidence(const uint8_t *user_data, size_t user_data_len,
                                  tdx_attestation_evidence_t **evidence)
{
    tdx_report_full_t report;
    tdx_attestation_evidence_t *generated;
    int result;

    if ((!user_data && user_data_len) || !evidence)
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    *evidence = NULL;
    result = tdx_attestation_generate_report(user_data, user_data_len, &report);
    if (result != TDX_ATTEST_SUCCESS)
        return result;
    generated = calloc(1, sizeof(*generated));
    if (!generated)
        return TDX_ATTEST_ERROR_NO_MEMORY;
    result = tdx_attestation_get_quote(&report, &generated->quote,
                                       &generated->quote_size);
    if (result != TDX_ATTEST_SUCCESS) {
        free(generated);
        return result;
    }
    generated->timestamp = (uint64_t)time(NULL);
    generated->tcb_status = TDX_TCB_STATUS_CONFIGURATION_NEEDED;
    *evidence = generated;
    return TDX_ATTEST_SUCCESS;
}

void
tdx_attestation_free_evidence(tdx_attestation_evidence_t *evidence)
{
    if (!evidence)
        return;
    free(evidence->quote);
    free(evidence->collateral);
    free(evidence);
}

int
tdx_attestation_get_td_info(tdx_report_full_t *report)
{
    return report ? tdx_attestation_generate_report(NULL, 0, report)
                  : TDX_ATTEST_ERROR_INVALID_PARAM;
}

int
tdx_attestation_extend_rtmr(uint32_t rtmr_index, const uint8_t *data, size_t len)
{
    if (rtmr_index >= 4 || !data || !len)
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    return TDX_ATTEST_ERROR_NOT_SUPPORTED;
}

int
tdx_attestation_get_report_data_hash(const uint8_t *data, size_t len,
                                     uint8_t hash[64])
{
    if ((!data && len) || !hash)
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    memset(hash, 0, 64);
    return sha384_hash(data, len, hash) ? TDX_ATTEST_SUCCESS
                                       : TDX_ATTEST_ERROR_REPORT_FAILURE;
}

int
tdx_attestation_platform_register(const char *registration_server)
{
    return registration_server ? TDX_ATTEST_ERROR_NOT_SUPPORTED
                               : TDX_ATTEST_ERROR_INVALID_PARAM;
}

int
tdx_attestation_get_platform_manifest(uint8_t **manifest, size_t *size)
{
    if (!manifest || !size)
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    *manifest = NULL;
    *size = 0;
    return TDX_ATTEST_ERROR_NOT_SUPPORTED;
}
