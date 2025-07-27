# TDX Platform Compilation Fix Guide

## Current Status

The TDX platform implementation has been created with comprehensive security features including attestation, memory encryption, and secure communication. However, there are compilation issues due to header conflicts with system libraries.

## Main Issues and Solutions

### 1. Socket Definition Conflicts
**Problem**: The TDX socket header was redefining structures that conflict with system headers.
**Solution**: Updated `tdx_socket.h` to:
- Include system socket headers on Linux
- Only define structures for non-Linux systems
- Renamed all socket functions to `tdx_*` prefix to avoid conflicts

### 2. Time Function Conflicts
**Problem**: Time functions conflicted with system time.h
**Solution**: Updated `tdx_time.h` to:
- Include system headers
- Use conditional compilation for constants
- Renamed all time functions to `tdx_*` prefix

### 3. File Operation Conflicts
**Problem**: File constants and structures conflicted with system headers
**Solution**: Updated `tdx_file.h` to:
- Include system headers (fcntl.h, dirent.h, etc.)
- Use conditional compilation for constants
- Removed duplicate DIR typedef

### 4. Pthread Integration
**Problem**: Pthread definitions conflicted with system pthread.h
**Solution**: Updated `tdx_pthread.h` to:
- Include system pthread.h directly
- Removed duplicate definitions
- TDX will use system pthread library

### 5. Signal Handling
**Problem**: Signal definitions conflicted with system signal.h
**Solution**: Updated `tdx_signal.h` to:
- Include system signal.h
- Renamed functions to `tdx_*` prefix
- Use system signal functions where possible

## Remaining Work

### Update WAMR Integration Layer

The main issue now is that other parts of the codebase expect the original function names (socket, bind, etc.) but we've renamed them to avoid conflicts. There are several approaches:

1. **Option 1: Update Platform API Layer**
   - Modify the WAMR platform API to use TDX-specific function names
   - Update `platform_api_vmcore.h` and related files

2. **Option 2: Create Wrapper Macros**
   - Define macros that map standard names to TDX functions when building for TDX
   - Example: `#define socket tdx_socket` when `BH_PLATFORM_LINUX_TDX` is defined

3. **Option 3: Use Weak Symbols**
   - Implement TDX functions as weak symbols that override system functions
   - Requires careful linking configuration

### Recommended Approach

Create a platform compatibility header that maps standard functions to TDX implementations:

```c
// tdx_platform_compat.h
#ifdef BH_PLATFORM_LINUX_TDX
  #define socket tdx_socket
  #define bind tdx_bind
  #define connect tdx_connect
  // ... etc
#endif
```

This header should be included after all system headers but before any WAMR code that uses these functions.

## Build Instructions

1. Ensure TDX SDK is installed:
   ```bash
   export TDX_SDK=/opt/intel/tdxsdk
   ```

2. Configure CMake (already set in CMakeLists.txt):
   ```cmake
   set(WAMR_BUILD_PLATFORM "linux-tdx")
   ```

3. Build:
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

## Testing

After compilation is fixed:
1. Test basic TDX detection
2. Test attestation flow
3. Test secure file operations
4. Test network operations through TDX
5. Verify memory encryption is active

## Security Notes

- All I/O operations go through TDX guest-host interface
- Memory is encrypted by hardware
- Attestation is required for trust establishment
- File operations should be validated at trust boundary