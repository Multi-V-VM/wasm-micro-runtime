# Linux TDX Platform Support

This directory contains the platform-specific implementation for running WAMR (WebAssembly Micro Runtime) in Intel TDX (Trust Domain Extensions) environments.

## Overview

Intel TDX is a CPU-based confidential computing technology that provides hardware-based memory encryption and integrity protection for virtual machines. This implementation adapts WAMR to run within TDX trust domains.

## Architecture

The TDX platform implementation follows a similar structure to the SGX implementation but uses TDX-specific interfaces:

- **Guest-side (Trusted)**: Code that runs inside the TDX trust domain
- **Host-side (Untrusted)**: Code that runs on the host and handles system calls

### Key Components

1. **tdx_wamr.tdl**: Defines the TDX guest-host interface (similar to SGX's EDL file)
2. **Platform files**: TDX-specific implementations of file, socket, pthread, time, and signal operations
3. **Untrusted directory**: Host-side implementations that handle actual system calls

## Files

### Core Platform Files
- `platform_internal.h`: Platform-specific type definitions and constants
- `tdx_platform.c`: Main platform initialization and memory management
- `shared_platform.cmake`: Build configuration for TDX platform

### TDX-Specific Implementations
- `tdx_file.h/c`: File I/O operations
- `tdx_pthread.h/c`: Threading support
- `tdx_socket.h/c`: Network socket operations
- `tdx_time.h/c`: Time-related functions
- `tdx_signal.h/c`: Signal handling
- `tdx_thread.c`: Thread management
- `tdx_ipfs.h/c`: IPFS (InterPlanetary File System) support

### Host-Side Implementations (untrusted/)
- `file.c`: Host-side file operations
- `pthread.c`: Host-side threading
- `socket.c`: Host-side networking
- `signal.c`: Host-side signal handling
- `time.c`: Host-side time operations
- `memory.c`: Host-side memory management

## Building

To build WAMR with TDX support:

1. Set the TDX SDK path:
   ```bash
   export TDX_SDK=/opt/intel/tdxsdk
   ```

2. Configure CMake with TDX platform:
   ```bash
   cmake -DWAMR_BUILD_PLATFORM=linux-tdx ..
   ```

3. Build:
   ```bash
   make
   ```

## Key Differences from SGX

1. **Interface Definition**: Uses TDX-specific guest-host calls (tdcall_*) instead of SGX ocalls
2. **Memory Management**: Adapted for TDX's memory protection model
3. **Threading**: Uses standard pthread with TDX-specific wrappers
4. **File Operations**: Implements secure file I/O through TDX interfaces

## Environment Variables

- `TDX_SDK`: Path to Intel TDX SDK (defaults to `/opt/intel/tdxsdk`)

## Security Considerations

1. All I/O operations go through the TDX guest-host interface
2. Memory is protected by TDX hardware encryption
3. File operations should be carefully validated as they cross the trust boundary

## Limitations

1. Signal handling is limited in TDX environment
2. Some system calls may have restricted functionality
3. Performance overhead for crossing trust boundary

## Future Enhancements

1. Optimize guest-host transitions
2. Add support for more TDX-specific features
3. Implement secure attestation support
4. Enhanced performance monitoring