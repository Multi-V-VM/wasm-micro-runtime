# Linux TDX Platform Implementation

This directory contains the Intel TDX (Trust Domain Extensions) platform implementation for WAMR, adapted from the Linux-SGX platform.

## Overview

Intel TDX is a CPU-based confidential computing technology that provides hardware-based memory encryption and integrity protection for virtual machines. This implementation allows WAMR to run inside TDX-protected environments.

## Features

- **Hardware-based Security**: Leverages Intel TDX for memory encryption and attestation
- **SGX Compatibility**: Adapted from the Linux-SGX platform for easy migration
- **Comprehensive Security Module**: Includes TDX detection, memory encryption, and attestation support
- **POSIX Compatibility**: Full support for file operations, sockets, pthread, and time functions

## Directory Structure

```
linux-tdx/
├── platform_internal.h      # Platform-specific type definitions
├── shared_platform.cmake    # Build configuration
├── tdx_wamr.tdl            # TDX interface definition
├── tdx_platform.c          # Main platform implementation
├── tdx_platform_compat.h   # POSIX compatibility definitions
├── tdx_file.h/c           # File operations with TDX wrappers
├── tdx_socket.h/c         # Socket operations
├── tdx_pthread.h/c        # Thread operations
├── tdx_time.h/c           # Time operations
├── tdx_signal.h/c         # Signal handling
├── tdx_security.h/c       # TDX security features
├── tdx_attestation.h/c    # Attestation support
├── tdx_ipfs.cpp           # IPFS integration
├── untrusted/             # Host-side implementations
└── TDX_BUILD_FIXES.md     # Build issue documentation
```

## Security Features

### 1. TDX Detection
- Automatic detection of TDX environment
- Verification of TDX capabilities

### 2. Memory Encryption
- All guest memory is encrypted by hardware
- Secure memory allocation wrappers

### 3. Attestation
- Remote attestation support with Intel DCAP
- Quote generation and verification
- Platform registration for multi-TD systems

### 4. Secure I/O
- All I/O operations go through TDX wrappers
- Trust boundary validation
- Security event logging

## Building

The TDX platform is automatically selected when building on Linux with TDX support:

```bash
mkdir build
cd build
cmake .. -DWAMR_BUILD_PLATFORM=linux-tdx
make
```

### Build Requirements

- Intel TDX SDK (default: /opt/intel/tdxsdk)
- GNU/Linux with TDX support
- CMake 3.10 or later

### Environment Variables

- `TDX_SDK`: Path to Intel TDX SDK (optional)

## Usage

### Basic Initialization

```c
// TDX is automatically detected and initialized
// No special initialization required
```

### Attestation Example

```c
#include "tdx_attestation.h"

// Generate attestation quote
uint8_t report_data[64] = {0};
size_t quote_size = 0;
uint8_t *quote = NULL;

tdx_error_t err = tdx_generate_quote(report_data, sizeof(report_data), 
                                     NULL, 0, &quote, &quote_size);
if (err == TDX_SUCCESS) {
    // Use the quote for remote attestation
}
```

### Security Status Check

```c
#include "tdx_security.h"

if (tdx_is_within_guest()) {
    printf("Running in TDX guest environment\n");
    
    tdx_guest_info_t info;
    if (tdx_get_guest_info(&info) == TDX_SUCCESS) {
        printf("TDX version: %u.%u\n", info.version_major, info.version_minor);
    }
}
```

## Implementation Notes

1. **Thread Safety**: All TDX operations are thread-safe
2. **Performance**: Minimal overhead for I/O operations
3. **Compatibility**: Drop-in replacement for Linux-SGX platform
4. **Security**: All sensitive operations are validated at trust boundary

## Known Limitations

1. TDX guest-host communication uses placeholder implementations (tdcall_*)
2. Full DCAP attestation requires additional setup
3. Some advanced TDX features are not yet exposed

## Future Enhancements

1. Real TDX guest-host communication implementation
2. Full Intel DCAP integration
3. Performance monitoring for TDX transitions
4. Enhanced security policies

## References

- [Intel TDX Documentation](https://www.intel.com/content/www/us/en/developer/tools/trust-domain-extensions/overview.html)
- [Canonical TDX Setup Guide](https://github.com/canonical/tdx)
- [WAMR Documentation](https://github.com/bytecodealliance/wasm-micro-runtime)

## License

SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
