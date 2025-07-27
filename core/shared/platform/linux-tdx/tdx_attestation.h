/*
 * Copyright (C) 2024 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _TDX_ATTESTATION_H
#define _TDX_ATTESTATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TDX Attestation structures based on Intel TDX specifications */

/* TD Report structure - 1024 bytes */
typedef struct __attribute__((packed)) {
    /* Report MAC structure - 256 bytes */
    struct {
        uint8_t report_type[4];
        uint8_t reserved1[12];
        uint8_t cpusvn[16];
        uint8_t tee_tcb_info_hash[48];
        uint8_t tee_info_hash[48];
        uint8_t report_data[64];
        uint8_t reserved2[32];
        uint8_t mac[32];
    } report_mac_struct;
    
    /* TEE TCB Info - 239 bytes */
    uint8_t tee_tcb_info[239];
    
    /* Reserved - 17 bytes */
    uint8_t reserved3[17];
    
    /* TEE Info - 512 bytes */
    struct {
        uint8_t attributes[8];
        uint8_t xfam[8];
        uint8_t mrtd[48];
        uint8_t mrconfigid[48];
        uint8_t mrowner[48];
        uint8_t mrownerconfig[48];
        uint8_t rtmr[4][48];
        uint8_t reserved[112];
    } td_info;
} tdx_report_full_t;

/* Quote header structure */
typedef struct __attribute__((packed)) {
    uint16_t version;
    uint16_t attestation_key_type;
    uint32_t tee_type;
    uint16_t reserved1;
    uint16_t pce_svn;
    uint8_t qe_svn;
    uint8_t reserved2[5];
    uint8_t user_data[20];
} tdx_quote_header_t;

/* DCAP Quote structure */
typedef struct {
    tdx_quote_header_t header;
    uint32_t report_size;
    uint8_t report_data[];  /* Variable length TD report */
} tdx_dcap_quote_t;

/* Attestation configuration */
typedef struct {
    char qgs_url[256];              /* Quote Generation Service URL */
    char pccs_url[256];             /* Provisioning Certificate Caching Service URL */
    uint32_t timeout_ms;            /* Timeout for attestation operations */
    bool use_qpl;                   /* Use Quote Provider Library */
    char collateral_path[256];      /* Path to collateral files */
} tdx_attestation_config_t;

/* Attestation evidence */
typedef struct {
    uint8_t *quote;
    size_t quote_size;
    uint8_t *collateral;
    size_t collateral_size;
    uint64_t timestamp;
    uint32_t tcb_status;
} tdx_attestation_evidence_t;

/* TCB (Trusted Computing Base) status codes */
#define TDX_TCB_STATUS_OK                    0x00
#define TDX_TCB_STATUS_OUT_OF_DATE          0x01
#define TDX_TCB_STATUS_REVOKED              0x02
#define TDX_TCB_STATUS_CONFIGURATION_NEEDED  0x03
#define TDX_TCB_STATUS_OUT_OF_DATE_CONFIG   0x04
#define TDX_TCB_STATUS_SW_HARDENING_NEEDED  0x05

/* Attestation API functions */

/* Initialize attestation subsystem */
int tdx_attestation_init(const tdx_attestation_config_t *config);
void tdx_attestation_cleanup(void);

/* Generate attestation report with custom data */
int tdx_attestation_generate_report(const uint8_t *user_data, size_t user_data_len,
                                   tdx_report_full_t *report);

/* Get quote from Quote Generation Service */
int tdx_attestation_get_quote(const tdx_report_full_t *report,
                             uint8_t **quote, size_t *quote_size);

/* Get collateral for quote verification */
int tdx_attestation_get_collateral(const uint8_t *quote, size_t quote_size,
                                  uint8_t **collateral, size_t *collateral_size);

/* Verify quote locally */
int tdx_attestation_verify_quote(const uint8_t *quote, size_t quote_size,
                                const uint8_t *collateral, size_t collateral_size,
                                uint32_t *tcb_status);

/* Generate complete attestation evidence */
int tdx_attestation_generate_evidence(const uint8_t *user_data, size_t user_data_len,
                                     tdx_attestation_evidence_t **evidence);

/* Free attestation evidence */
void tdx_attestation_free_evidence(tdx_attestation_evidence_t *evidence);

/* Helper functions */
int tdx_attestation_get_td_info(tdx_report_full_t *report);
int tdx_attestation_extend_rtmr(uint32_t rtmr_index, const uint8_t *data, size_t len);
int tdx_attestation_get_report_data_hash(const uint8_t *data, size_t len, 
                                        uint8_t hash[64]);

/* Platform registration (for Multi-package systems) */
int tdx_attestation_platform_register(const char *registration_server);
int tdx_attestation_get_platform_manifest(uint8_t **manifest, size_t *size);

/* Error codes */
#define TDX_ATTEST_SUCCESS                   0
#define TDX_ATTEST_ERROR_NOT_INITIALIZED    -100
#define TDX_ATTEST_ERROR_INVALID_PARAM      -101
#define TDX_ATTEST_ERROR_NO_MEMORY          -102
#define TDX_ATTEST_ERROR_REPORT_FAILURE     -103
#define TDX_ATTEST_ERROR_QUOTE_FAILURE      -104
#define TDX_ATTEST_ERROR_NETWORK            -105
#define TDX_ATTEST_ERROR_VERIFICATION       -106
#define TDX_ATTEST_ERROR_NOT_SUPPORTED      -107

#ifdef __cplusplus
}
#endif

#endif /* _TDX_ATTESTATION_H */