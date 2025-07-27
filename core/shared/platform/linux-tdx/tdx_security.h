/*
 * Copyright (C) 2024 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _TDX_SECURITY_H
#define _TDX_SECURITY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TDX Security Configuration and Management */

/* TDX guest detection and capabilities */
typedef struct {
    bool is_tdx_guest;
    uint32_t tdx_version;
    uint32_t security_features;
    bool memory_encryption_enabled;
    bool attestation_available;
    bool secure_boot_enabled;
} tdx_security_info_t;

/* Attestation report structure */
typedef struct {
    uint8_t report_mac_struct[256];
    uint8_t report_data[64];
    uint8_t reserved[32];
} tdx_report_t;

/* Quote generation structure */
typedef struct {
    uint16_t version;
    uint16_t status;
    uint32_t in_len;
    uint32_t out_len;
    uint8_t data[4096];
} tdx_quote_t;

/* Security configuration flags */
#define TDX_SEC_MEMORY_ENCRYPTION   0x0001
#define TDX_SEC_ATTESTATION        0x0002
#define TDX_SEC_SECURE_BOOT        0x0004
#define TDX_SEC_MEASURED_BOOT      0x0008
#define TDX_SEC_SEALED_STORAGE     0x0010

/* TDX Module calls for security operations */
#define TDG_VP_INFO                0x00000001
#define TDG_MR_REPORT              0x00000004
#define TDG_VM_RD                  0x00000005
#define TDG_VM_WR                  0x00000006

/* Security initialization and detection */
int tdx_security_init(void);
void tdx_security_cleanup(void);
bool tdx_is_guest(void);
int tdx_get_security_info(tdx_security_info_t *info);

/* Attestation functions */
int tdx_generate_report(const uint8_t *report_data, size_t report_data_len,
                       tdx_report_t *report);
int tdx_get_quote(const tdx_report_t *report, tdx_quote_t *quote);
int tdx_verify_quote(const tdx_quote_t *quote, bool *verified);

/* Memory protection */
int tdx_protect_memory_region(void *addr, size_t size, uint32_t flags);
int tdx_unprotect_memory_region(void *addr, size_t size);
bool tdx_is_memory_encrypted(void *addr, size_t size);

/* Secure boot verification */
int tdx_verify_secure_boot(void);
int tdx_get_boot_measurements(uint8_t *measurements, size_t *size);

/* Key management */
int tdx_derive_key(const uint8_t *label, size_t label_len,
                   uint8_t *key, size_t key_len);
int tdx_seal_data(const uint8_t *data, size_t data_len,
                  uint8_t *sealed_data, size_t *sealed_len);
int tdx_unseal_data(const uint8_t *sealed_data, size_t sealed_len,
                    uint8_t *data, size_t *data_len);

/* Guest-host communication security */
int tdx_secure_channel_init(void);
int tdx_secure_channel_send(const uint8_t *data, size_t len);
int tdx_secure_channel_recv(uint8_t *data, size_t *len);
void tdx_secure_channel_cleanup(void);

/* Error codes */
#define TDX_SUCCESS                 0
#define TDX_ERROR_NOT_SUPPORTED    -1
#define TDX_ERROR_INVALID_PARAM    -2
#define TDX_ERROR_NO_MEMORY        -3
#define TDX_ERROR_ATTESTATION      -4
#define TDX_ERROR_CRYPTO           -5
#define TDX_ERROR_COMMUNICATION    -6
#define TDX_ERROR_NOT_TDX_GUEST    -7

#ifdef __cplusplus
}
#endif

#endif /* _TDX_SECURITY_H */