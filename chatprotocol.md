# Chat Protocol

This document tracks user requests and the actions taken in response on the `work` branch.

## 2025-10-13
- **Request:** Create `chatprotocol.md` on the live branch, keep it updated with request/action history.
- **Action:** Added this file at the repository root and documented the tracking process.
- **Request:** Vulkan UI renders blurry or cropped at 150% Windows scale; make it crisp without oversizing.
- **Action:** Updated Windows host scale detection to honor the monitor's device DPI when the host window is DPI-unaware, allowing the parent and Vulkan child windows to size consistently with the render scale.
- **Request:** Add exhaustive logging from DPI discovery through Vulkan swapchain creation to diagnose blurred rendering.
- **Action:** Instrumented DPI scale helpers, window sizing routines, and Vulkan context/swapchain code with structured logs capturing window handles, scales, and size transitions.
- **Request:** Resolve build errors caused by the new Vulkan logging instrumentation (SetWindowPos return type and ambiguous MakeField overload).
- **Action:** Added a DPI-aware SetWindowPos helper that preserves WDL behavior while returning success, updated callers to use it, and tightened logging fields to compile cleanly.
- **Request:** Remaining build errors report `SetWindowPosWithResult` returning `void` and an ambiguous `vulkanlog::MakeField` overload.
- **Action:** Loaded the real Win32 `SetWindowPos` via `GetProcAddress` to obtain reliable BOOL results, adjusted the helper to restore the WDL macro safely, and disambiguated the Vulkan logging fields with explicit types.

