# Mobile Bridge API Review

## 1. Overview

This document provides a technical review of the `mobile_bridge.h` interface.

## 2. High-Level Assessment

The current interface relies on C++-specific constructs such as std::vector, std::string, and std::function, which are not ABI-stable and cannot be safely used across language boundaries. This makes the interface unsuitable for direct use from non-C++ environments (e.g., Java/Kotlin on Android or Swift on iOS) without a wrapper.

Without proper isolation or C-compatible bridges, this could lead to memory corruption, crashes, or undefined behavior — especially if mobile code allocates or frees memory across ABI boundaries.

sendRawData() vs processIncomingData() is unclear. Add comments or document clearly.

## 3. Suggestions
-  **The suggestion is to refactor the interface into a pure C API.** This provides a stable, well-defined contract that all languages can safely bind to.
-  Document memory ownership when pointers are passed around.
-  Add callbacks to get the status of the ECU, to notify when it is up and running.
-  Add Error and timestamp in struct Response, for debugging. Add structured error codes taht maps to the error.
-  Document and enforce thread safety
