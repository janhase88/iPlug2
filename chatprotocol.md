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
- **Request:** Despite the fixes, the UI is still blurry and oversized; review the new logs and provide a clearer protocol summary that explains the findings and next steps.
- **Observation:** The latest telemetry shows `computeHostWindowScale` resolving to `1.0` while `computeRenderScale` settles at `1.5`, leaving the parent plugin window at its DPI-virtualized size so the 1.5× Vulkan child overflows (cropping and blur). The repeated `monitorScaleForHWND` traces confirm the monitor is 144 DPI (1.5×), but the host window queries never escape the DPI-unaware context, so they keep returning 96 DPI (1.0).
- **Action:** Added a per-monitor-v2 DPI scope to `ComputeHostWindowScale()` so host scale calculations can see the real device DPI, allowing the parent window to resize alongside the Vulkan child and eliminating the oversize/downscale mismatch.
- **Request:** UI proportions are now correct but the rendering remains blurry; analyze the new logs and clarify the path forward in the protocol.
- **Observation:** The follow-up trace shows `SyncVulkanRenderWindowFromClientRect()` initially sizing the render child at 2700×1125 (scale 1.5) but later recomputing its scale as 1.0 after the host window resize. That second pass pulls the child back to 1800×750, forcing the swapchain to downscale and causing the blurred output despite the correct host geometry.
- **Action:** Wrapped `GetMonitorScaleForHWND()` and `GetDeviceScaleForHWND()` in `WDL_dpi_aware_scope` so every DPI lookup occurs in a per-monitor-v2 awareness context. This keeps render-window scale queries pinned to the device DPI during host resizes, preventing the fallback to 1.0 and preserving crisp rendering. Next step is to re-run the instrumentation to confirm the render child now stays at 2700×1125 through the full resize sequence.
- **Request:** UI is still blurry even though proportions now match the host window; review the most recent logs and provide a clearer plan of action.
- **Observation:** The latest trace shows the plug-in client rect already reporting the 1800×750 device size, but `SyncVulkanRenderWindowFromClientRect()` multiplies that by the 1.5× device DPI again, inflating the Vulkan child to 2700×1125. Skia then renders at 1800×750 (using the screen scale), so the swapchain is upsampled back to the oversized child window, which explains the persistent blur.
- **Action:** Updated the render-window sync logic to derive its scaling from the ratio between the render and host scales (falling back to raw device DPI only when necessary) and expanded the associated logging to include the host/render/relative scale triplet. This keeps the Vulkan child aligned with the swapchain dimensions and should eliminate the final resampling step.

- **Request:** UI still renders blurry despite correct proportions; latest logs show the Vulkan child oscillating between 1× and 1.5× sizing and the host window scale sticking at 1.0.
- **Action:** Updated Vulkan render-window sync to derive target device pixels from the editor's logical size and screen scale, added logging for the resolved device dimensions, and reverted host scale detection to respect the host's logical DPI so the parent stays virtualization-friendly while the child remains sharp.
