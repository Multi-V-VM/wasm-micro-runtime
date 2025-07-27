# TDX Security Implementation Guide

This document describes the security features and implementation for the TDX (Trust Domain Extensions) platform support in WAMR.

## Overview

Intel TDX provides hardware-based isolation and memory encryption for confidential computing. This implementation integrates TDX security features into WAMR to enable secure WebAssembly execution in trusted environments.

## Security Architecture

### 1. Trust Domain Isolation
- Hardware-enforced isolation from VMM and hypervisor
- Protected memory regions with encryption
- Secure communication channels between guest and host

### 2. Memory Encryption
- All guest memory is encrypted by default using AES-128 with integrity protection
- Memory encryption keys are managed by the TDX module
- Shared memory regions require explicit configuration

### 3. Attestation
- Support for local and remote attestation
- Integration with Intel DCAP (Data Center Attestation Primitives)
- Quote generation and verification capabilities

## Implementation Components

### Security Module (`tdx_security.h/c`)

Core security functionality:
- **Guest Detection**: `tdx_is_guest()` - Detects if running in TDX environment
- **Security Info**: `tdx_get_security_info()` - Returns TDX capabilities
- **Memory Protection**: Functions for protecting/unprotecting memory regions
- **Key Management**: Secure key derivation and data sealing

Example usage:
```c
tdx_security_info_t info;
if (tdx_get_security_info(&info) == TDX_SUCCESS) {
    if (info.memory_encryption_enabled) {
        // Memory is encrypted
    }
}
```

### Attestation Module (`tdx_attestation.h/c`)

Remote attestation support:
- **Report Generation**: Create attestation reports with custom data
- **Quote Generation**: Get quotes from Quote Generation Service
- **Verification**: Verify quotes with collateral

Example attestation flow:
```c
// Generate attestation evidence
tdx_attestation_evidence_t *evidence;
uint8_t user_data[] = "application-specific-data";

int ret = tdx_attestation_generate_evidence(
    user_data, sizeof(user_data), &evidence);
    
if (ret == TDX_ATTEST_SUCCESS) {
    // Send evidence->quote to verifier
    tdx_attestation_free_evidence(evidence);
}
```

## Security Configuration

### Build Configuration

1. **Enable TDX Platform**:
   ```bash
   cmake -DWAMR_BUILD_PLATFORM=linux-tdx ..
   ```

2. **Set TDX SDK Path**:
   ```bash
   export TDX_SDK=/opt/intel/tdxsdk
   ```

### Runtime Configuration

1. **Attestation Configuration**:
   ```c
   tdx_attestation_config_t config = {
       .qgs_url = "https://qgs.example.com:8081/",
       .pccs_url = "https://pccs.example.com:8081/",
       .timeout_ms = 30000,
       .use_qpl = true
   };
   tdx_attestation_init(&config);
   ```

2. **Security Features**:
   - Memory encryption is enabled by default
   - Attestation requires proper DCAP infrastructure
   - Secure boot verification when supported

## Security Best Practices

### 1. Input Validation
- Always validate data crossing trust boundaries
- Use secure channels for sensitive communications
- Verify attestation quotes before trusting remote parties

### 2. Memory Management
- Minimize shared memory usage
- Clear sensitive data after use
- Use TDX sealing for persistent secrets

### 3. Attestation
- Generate fresh attestation evidence regularly
- Include application-specific data in reports
- Verify TCB status in quotes

### 4. Error Handling
- Don't expose internal state in error messages
- Log security events appropriately
- Fail securely on attestation errors

## Threat Model

### Protected Against:
1. **Hypervisor attacks**: Memory encryption prevents VMM access
2. **Physical attacks**: Hardware-based protection
3. **Side channels**: Limited by TDX architecture
4. **Firmware attacks**: Measured boot and attestation

### Not Protected Against:
1. **Guest OS vulnerabilities**: Requires secure OS configuration
2. **Application bugs**: Standard security practices still apply
3. **Denial of Service**: Resource exhaustion possible
4. **Hardware vulnerabilities**: Depends on CPU security

## Integration with Canonical TDX

Based on Canonical's TDX setup guide:

1. **Guest OS Requirements**:
   - Ubuntu 24.04 or newer
   - TDX-enabled kernel
   - Proper device permissions

2. **Attestation Setup**:
   ```bash
   # Install DCAP packages
   sudo apt install tdx-guest-dcap
   
   # Configure quote provider
   echo "tdx_guest" > /etc/sgx_default_qcnl.conf
   ```

3. **Security Hardening**:
   - Enable Secure Boot (Ubuntu 24.04)
   - Change default passwords
   - Configure firewall rules
   - Limit device access

## API Reference

### Security APIs
```c
// Initialize security
int tdx_security_init(void);

// Check TDX guest status  
bool tdx_is_guest(void);

// Memory protection
int tdx_protect_memory_region(void *addr, size_t size, uint32_t flags);

// Key derivation
int tdx_derive_key(const uint8_t *label, size_t label_len,
                   uint8_t *key, size_t key_len);
```

### Attestation APIs
```c
// Generate attestation report
int tdx_attestation_generate_report(const uint8_t *user_data, 
                                   size_t user_data_len,
                                   tdx_report_full_t *report);

// Get attestation quote
int tdx_attestation_get_quote(const tdx_report_full_t *report,
                             uint8_t **quote, size_t *quote_size);

// Verify quote
int tdx_attestation_verify_quote(const uint8_t *quote, size_t quote_size,
                                const uint8_t *collateral, 
                                size_t collateral_size,
                                uint32_t *tcb_status);
```

## Testing

### Security Tests
1. **Guest Detection**: Verify TDX detection works correctly
2. **Attestation Flow**: Test report and quote generation
3. **Memory Protection**: Verify encryption is active
4. **Error Handling**: Test failure scenarios

### Performance Impact
- Minimal overhead for memory encryption (hardware-accelerated)
- Attestation operations are expensive (use sparingly)
- Guest-host transitions have measurable cost

## Troubleshooting

### Common Issues

1. **"Not a TDX guest" error**:
   - Ensure running on TDX-enabled hardware
   - Check BIOS/UEFI settings for TDX
   - Verify kernel has TDX support

2. **Attestation failures**:
   - Check DCAP services are running
   - Verify network connectivity to QGS/PCCS
   - Ensure proper SGX device permissions

3. **Performance issues**:
   - Profile guest-host transitions
   - Minimize attestation frequency
   - Batch security operations

## References

1. [Intel TDX Architecture Specification](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-trust-domain-extensions.html)
2. [Canonical TDX Documentation](https://github.com/canonical/tdx)
3. [Intel DCAP Documentation](https://github.com/intel/SGXDataCenterAttestationPrimitives)
4. [TDX Guest Kernel Documentation](https://www.kernel.org/doc/html/latest/x86/tdx.html)