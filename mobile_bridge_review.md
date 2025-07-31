# Mobile Bridge API Review

## 1. Overview

This document provides a technical review of the `mobile_bridge.h` interface. The goal is to identify potential issues related to cross-platform stability, maintainability, and ease of integration with mobile clients (Android/Kotlin, iOS/Swift) and to propose concrete enhancements.

## 2. High-Level Assessment

The current interface is well-intentioned but uses C++-specific features (`std::string`, `std::vector`, `std::function`, class-based interface) that are not suitable for a stable, cross-language bridge. These features create a fragile ABI (Application Binary Interface) that can lead to memory corruption, crashes, and undefined behavior when called from non-C++ languages.

**The core recommendation is to refactor the interface into a pure C API.** This provides a stable, well-defined contract that all languages can safely bind to.

## 3. Identified Issues

### Issue #1: C++ Class Interface (`IProtocolEngine`)

-   **Problem**: Exposing a C++ class directly is not ABI-stable. C++ name mangling, vtable layout, and exception handling are compiler-specific and can break between different compilers or even different versions of the same compiler.
-   **Risk**: High risk of crashes and undefined behavior when the C++ library is updated independently of the mobile app.

### Issue #2: Non-Portable Data Types (`std::string`, `std::vector`)

-   **Problem**: The memory layout of C++ standard library types is not guaranteed. Passing them across a language boundary (e.g., from C++ to Swift) means the receiving language has no safe way to interpret the object's internal structure.
-   **Risk**: High risk of memory corruption, buffer overflows, and crashes.

### Issue #3: Non-Portable Callbacks (`std::function`)

-   **Problem**: `std::function` is a complex C++ object. It cannot be represented or called from C, Java, or Swift.
-   **Risk**: This makes the asynchronous API impossible to implement safely on mobile platforms.

## 4. Proposed Enhancements for Stability

### Enhancement #1: Transition to a Pure C Interface (Opaque Pointer)

Instead of returning a pointer to a C++ class, the API should return an **opaque pointer** (`VdpEngineHandle*`). The client treats this as a handle and never dereferences it. All API functions will take this handle as their first parameter.

```c
// Opaque handle to the engine
typedef struct VdpEngineHandle VdpEngineHandle;

// All functions operate on the handle
VdpEngineHandle* vdp_create_engine();
bool vdp_initialize(VdpEngineHandle* handle, const char* device_path);
void vdp_destroy_engine(VdpEngineHandle* handle);
```

### Enhancement #2: Use C-Compatible Data Types

All function signatures should use only C-compatible primitive types.

-   **Strings**: Use `const char*` for input. For output, use a caller-provided buffer (`char* buffer, size_t buffer_len`).
-   **Byte Arrays**: Use `const uint8_t* data, size_t len`.

```c
// Before (Unsafe)
virtual Response sendFrame(const Frame& frame, uint32_t timeout_ms);

// After (Safe)
// The response is returned via output parameters.
int32_t vdp_send_frame(VdpEngineHandle* handle, 
                       const uint8_t* request_data, size_t request_len,
                       uint8_t* response_buffer, size_t response_buffer_len,
                       uint32_t timeout_ms);
```

### Enhancement #3: Implement C-Style Callbacks

Replace `std::function` with a C-style function pointer and a `void*` context pointer. This allows the mobile client to pass a pointer to its own state or context, which is then passed back in the callback.

```c
// C-style callback definition
typedef void (*vdp_response_callback_t)(const uint8_t* data, size_t len, void* user_context);

// Asynchronous function signature
void vdp_send_frame_async(VdpEngineHandle* handle, 
                          const uint8_t* request_data, size_t request_len,
                          vdp_response_callback_t on_response,
                          void* user_context);
```

These changes will result in a robust, portable, and easy-to-maintain mobile bridge that aligns with industry best practices for cross-language development.
