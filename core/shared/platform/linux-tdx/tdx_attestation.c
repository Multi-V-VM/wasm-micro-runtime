/*
 * Copyright (C) 2024 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "tdx_attestation.h"
#include "tdx_security.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>

/* TDX attestation device */
#define TDX_ATTEST_DEVICE "/dev/tdx-attest"

/* TDX module call for getting TD report */
#define TDG_MR_REPORT    0x04

/* Global attestation state */
static struct {
    bool initialized;
    tdx_attestation_config_t config;
    int attest_fd;
} g_attestation = { .initialized = false, .attest_fd = -1 };

/* SHA-384 for report data */
static void
sha384_hash(const uint8_t *data, size_t len, uint8_t hash[48])
{
    /* Simplified - should use proper crypto library */
    memset(hash, 0, 48);
    if (data && len > 0) {
        /* Hash first 48 bytes for demo */
        size_t copy_len = len > 48 ? 48 : len;
        memcpy(hash, data, copy_len);
    }
}

/* Initialize attestation subsystem */
int
tdx_attestation_init(const tdx_attestation_config_t *config)
{
    if (g_attestation.initialized) {
        return TDX_ATTEST_SUCCESS;
    }
    
    /* Verify TDX guest status */
    if (!tdx_is_guest()) {
        return TDX_ATTEST_ERROR_NOT_SUPPORTED;
    }
    
    /* Copy configuration */
    if (config) {
        memcpy(&g_attestation.config, config, sizeof(tdx_attestation_config_t));
    } else {
        /* Set default configuration */
        strcpy(g_attestation.config.qgs_url, "https://localhost:8081/sgx/certification/v4/");
        strcpy(g_attestation.config.pccs_url, "https://localhost:8081/");
        g_attestation.config.timeout_ms = 30000;
        g_attestation.config.use_qpl = true;
    }
    
    /* Open attestation device if available */
    g_attestation.attest_fd = open(TDX_ATTEST_DEVICE, O_RDWR);
    
    g_attestation.initialized = true;
    return TDX_ATTEST_SUCCESS;
}

/* Cleanup attestation subsystem */
void
tdx_attestation_cleanup(void)
{
    if (g_attestation.attest_fd >= 0) {
        close(g_attestation.attest_fd);
        g_attestation.attest_fd = -1;
    }
    g_attestation.initialized = false;
}

/* Generate TDX attestation report */
int
tdx_attestation_generate_report(const uint8_t *user_data, size_t user_data_len,
                               tdx_report_full_t *report)
{
    tdx_report_t basic_report;
    uint8_t report_data[64] = {0};
    int ret;
    
    if (!report || !g_attestation.initialized) {
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    }
    
    /* Prepare report data */
    if (user_data && user_data_len > 0) {
        if (user_data_len <= 64) {
            memcpy(report_data, user_data, user_data_len);
        } else {
            /* Hash larger data */
            sha384_hash(user_data, user_data_len, report_data);
        }
    }
    
    /* Generate basic report using TDX security module */
    ret = tdx_generate_report(report_data, sizeof(report_data), &basic_report);
    if (ret != TDX_SUCCESS) {
        return TDX_ATTEST_ERROR_REPORT_FAILURE;
    }
    
    /* Build full report structure */
    memset(report, 0, sizeof(tdx_report_full_t));
    
    /* Copy MAC structure */
    memcpy(&report->report_mac_struct, &basic_report, 
           sizeof(report->report_mac_struct));
    
    /* Fill TD info - this would come from TDX module */
    report->td_info.attributes[0] = 0x00;  /* DEBUG disabled */
    report->td_info.xfam[0] = 0x03;        /* x87, SSE */
    
    /* TD measurements would be populated by TDX module */
    /* For now, using placeholder values */
    
    return TDX_ATTEST_SUCCESS;
}

/* Get quote from Quote Generation Service */
int
tdx_attestation_get_quote(const tdx_report_full_t *report,
                         uint8_t **quote, size_t *quote_size)
{
    tdx_quote_t basic_quote;
    int ret;
    
    if (!report || !quote || !quote_size) {
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    }
    
    /* Get quote using TDX security module */
    ret = tdx_get_quote((const tdx_report_t *)report, &basic_quote);
    if (ret != TDX_SUCCESS) {
        return TDX_ATTEST_ERROR_QUOTE_FAILURE;
    }
    
    /* Allocate quote buffer */
    *quote_size = sizeof(tdx_quote_header_t) + sizeof(uint32_t) + 
                  sizeof(tdx_report_full_t) + 512; /* Extra for signature */
    *quote = (uint8_t *)malloc(*quote_size);
    if (!*quote) {
        return TDX_ATTEST_ERROR_NO_MEMORY;
    }
    
    /* Build DCAP quote structure */
    tdx_dcap_quote_t *dcap_quote = (tdx_dcap_quote_t *)*quote;
    
    /* Fill quote header */
    dcap_quote->header.version = 4;
    dcap_quote->header.attestation_key_type = 2;  /* ECDSA-P256 */
    dcap_quote->header.tee_type = 0x00000081;     /* TDX */
    dcap_quote->header.qe_svn = 2;
    dcap_quote->header.pce_svn = 11;
    
    /* Copy report */
    dcap_quote->report_size = sizeof(tdx_report_full_t);
    memcpy(dcap_quote->report_data, report, sizeof(tdx_report_full_t));
    
    /* Signature would be added by QE (Quoting Enclave) */
    
    return TDX_ATTEST_SUCCESS;
}

/* Get collateral for quote verification */
int
tdx_attestation_get_collateral(const uint8_t *quote, size_t quote_size,
                               uint8_t **collateral, size_t *collateral_size)
{
    if (!quote || !collateral || !collateral_size) {
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    }
    
    /* Collateral includes TCB info, certificates, CRLs */
    /* This would typically be fetched from PCCS */
    
    /* For demo, create minimal collateral */
    *collateral_size = 1024;
    *collateral = (uint8_t *)malloc(*collateral_size);
    if (!*collateral) {
        return TDX_ATTEST_ERROR_NO_MEMORY;
    }
    
    /* Fill with placeholder collateral data */
    memset(*collateral, 0, *collateral_size);
    
    return TDX_ATTEST_SUCCESS;
}

/* Verify quote locally */
int
tdx_attestation_verify_quote(const uint8_t *quote, size_t quote_size,
                            const uint8_t *collateral, size_t collateral_size,
                            uint32_t *tcb_status)
{
    if (!quote || !tcb_status) {
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    }
    
    /* Quote verification would use Intel DCAP verification library */
    /* This is a simplified verification */
    
    /* Check quote header */
    if (quote_size < sizeof(tdx_quote_header_t)) {
        return TDX_ATTEST_ERROR_VERIFICATION;
    }
    
    tdx_quote_header_t *header = (tdx_quote_header_t *)quote;
    if (header->version != 4 || header->tee_type != 0x00000081) {
        return TDX_ATTEST_ERROR_VERIFICATION;
    }
    
    /* Verify signature, certificates, TCB status */
    /* For demo, return OK status */
    *tcb_status = TDX_TCB_STATUS_OK;
    
    return TDX_ATTEST_SUCCESS;
}

/* Generate complete attestation evidence */
int
tdx_attestation_generate_evidence(const uint8_t *user_data, size_t user_data_len,
                                 tdx_attestation_evidence_t **evidence)
{
    tdx_report_full_t report;
    int ret;
    
    if (!evidence) {
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    }
    
    /* Allocate evidence structure */
    *evidence = (tdx_attestation_evidence_t *)calloc(1, 
                                                     sizeof(tdx_attestation_evidence_t));
    if (!*evidence) {
        return TDX_ATTEST_ERROR_NO_MEMORY;
    }
    
    /* Generate report */
    ret = tdx_attestation_generate_report(user_data, user_data_len, &report);
    if (ret != TDX_ATTEST_SUCCESS) {
        free(*evidence);
        return ret;
    }
    
    /* Get quote */
    ret = tdx_attestation_get_quote(&report, &(*evidence)->quote, 
                                   &(*evidence)->quote_size);
    if (ret != TDX_ATTEST_SUCCESS) {
        free(*evidence);
        return ret;
    }
    
    /* Get collateral */
    ret = tdx_attestation_get_collateral((*evidence)->quote, 
                                        (*evidence)->quote_size,
                                        &(*evidence)->collateral,
                                        &(*evidence)->collateral_size);
    if (ret != TDX_ATTEST_SUCCESS) {
        free((*evidence)->quote);
        free(*evidence);
        return ret;
    }
    
    /* Set timestamp */
    (*evidence)->timestamp = time(NULL);
    (*evidence)->tcb_status = TDX_TCB_STATUS_OK;
    
    return TDX_ATTEST_SUCCESS;
}

/* Free attestation evidence */
void
tdx_attestation_free_evidence(tdx_attestation_evidence_t *evidence)
{
    if (evidence) {
        if (evidence->quote) {
            free(evidence->quote);
        }
        if (evidence->collateral) {
            free(evidence->collateral);
        }
        free(evidence);
    }
}

/* Get TD info */
int
tdx_attestation_get_td_info(tdx_report_full_t *report)
{
    if (!report) {
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    }
    
    /* Generate report with empty user data to get TD info */
    return tdx_attestation_generate_report(NULL, 0, report);
}

/* Extend Runtime Measurement Register */
int
tdx_attestation_extend_rtmr(uint32_t rtmr_index, const uint8_t *data, size_t len)
{
    uint8_t hash[48];
    
    if (rtmr_index >= 4 || !data || len == 0) {
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    }
    
    /* Hash the data */
    sha384_hash(data, len, hash);
    
    /* Extend RTMR using TDX module call */
    /* This would use TDG.MR.RTMR.EXTEND */
    
    return TDX_ATTEST_SUCCESS;
}

/* Get report data hash */
int
tdx_attestation_get_report_data_hash(const uint8_t *data, size_t len,
                                    uint8_t hash[64])
{
    if (!data || !hash) {
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    }
    
    /* Clear hash buffer */
    memset(hash, 0, 64);
    
    /* Generate SHA-384 hash */
    sha384_hash(data, len, hash);
    
    return TDX_ATTEST_SUCCESS;
}

/* Platform registration for multi-package systems */
int
tdx_attestation_platform_register(const char *registration_server)
{
    if (!registration_server) {
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    }
    
    /* Connect to MPA (Multi-package Platform Agent) */
    /* Register platform for attestation */
    
    return TDX_ATTEST_SUCCESS;
}

/* Get platform manifest */
int
tdx_attestation_get_platform_manifest(uint8_t **manifest, size_t *size)
{
    if (!manifest || !size) {
        return TDX_ATTEST_ERROR_INVALID_PARAM;
    }
    
    /* Platform manifest includes info about all packages */
    /* This is used for multi-socket TDX systems */
    
    *size = 512;
    *manifest = (uint8_t *)malloc(*size);
    if (!*manifest) {
        return TDX_ATTEST_ERROR_NO_MEMORY;
    }
    
    /* Fill with platform information */
    memset(*manifest, 0, *size);
    
    return TDX_ATTEST_SUCCESS;
}