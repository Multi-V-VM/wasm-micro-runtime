# TDX Platform Build Fixes

This document summarizes the fixes applied to resolve compilation errors in the TDX platform implementation.

## Fixes Applied

### 1. **pthread Function Conflicts**
**Problem**: tdx_pthread.c was defining standard pthread functions that conflicted with system headers.
**Solution**: Removed all pthread function implementations since TDX uses the system pthread library directly.

### 2. **Thread Local Attribute**
**Problem**: `os_thread_local_attribute` was undefined.
**Solution**: Added compiler-specific definition for thread-local storage:
```c
#if defined(__GNUC__) || defined(__clang__)
    #define os_thread_local_attribute __thread
#else
    #define os_thread_local_attribute
#endif
```

### 3. **bh_hash_map_create Function**
**Problem**: Wrong number of arguments passed to `bh_hash_map_create`.
**Solution**: Added the missing sixth parameter (NULL) to match the function signature.

### 4. **Missing tdcall Declarations**
**Problem**: tdcall functions were used before declaration.
**Solution**: 
- Added forward declarations at the top of tdx_platform.c
- Added missing directory operation declarations in tdx_file.c

### 5. **POSIX File Operations**
**Problem**: Missing POSIX functions like `preadv`, `pwritev`, `poll`, etc.
**Solution**: 
- Added `-D_GNU_SOURCE` and `-D_DEFAULT_SOURCE` to enable POSIX features
- Created tdx_platform_compat.h with necessary includes

## Remaining Considerations

### System Integration
The TDX platform now uses system libraries for:
- pthread operations
- Socket operations (with TDX wrappers)
- File operations (with TDX wrappers)
- Time operations (with TDX wrappers)

### Security Features
All I/O operations go through TDX wrappers that can:
1. Validate operations at the trust boundary
2. Log security-relevant events
3. Enforce TDX-specific policies

### Build Configuration
The platform is configured in CMakeLists.txt to use "linux-tdx" when `LINUX` is defined.

## Testing Recommendations

1. Verify TDX detection works correctly
2. Test file operations through TDX wrappers
3. Verify attestation flow
4. Check memory encryption status
5. Test network operations

## Next Steps

1. Implement actual TDX guest-host communication (currently using placeholder implementations)
2. Add real attestation support with Intel DCAP
3. Implement secure channel communication
4. Add performance monitoring for TDX transitions