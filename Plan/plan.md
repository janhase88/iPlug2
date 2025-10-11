# Windows VST3 Skia/Vulkan DPI Plan

## Phase 1 – Investigation ✅
- Confirmed the host was still feeding the virtual 96 DPI scale when DPI virtualization is active.
- Identified the monitor's physical pixel size via `EnumDisplaySettingsW` so we can derive the virtualization ratio.

## Phase 2 – Instrumentation ✅
- [x] Emit `DBGMSG` telemetry from `GetScaleForHWND`/`IGraphicsWin` so every DPI query shows host DPI, virtualization ratio, and effective scale.
- [x] Add Skia bitmap/surface diagnostics to track logical vs render pixels for each swapchain image.
- [x] Promote the Vulkan logger to verbose mode and annotate swapchain extent selection so we can see when caps clamp the render target.
- [x] Split host layout DPI from render DPI inside `IGraphicsWin`, expose the render scale to the backend, and update Skia/Vulkan surfaces to size themselves from the backing pixel scale instead of the logical host scale.
- [x] Propagate the backing pixel scale through layer creation and bitmap caching so offscreen surfaces no longer report the virtual 1.0 scale.

## Phase 3 – Validation 🚧
- [ ] Run a Windows VST3 build at 100 % and 150 % scale, capture the new backend/Skia/Vulkan logs, and confirm the swapchain extent matches the physical pixels now that host vs. render DPI are separated.
- [ ] Verify that the editor HWND hierarchy remains at the host-reported logical size while the Skia/Vulkan render targets jump to the physical pixel dimensions (e.g. 1800×750 at 150 %) and each control stays inside its logical bounds.
- [ ] If the swapchain still reports the virtual extent or the window resizes unexpectedly, trace through the new log fields to decide whether to adjust the render/layout split or dive into Skia image provider / Vulkan renderpass configuration next.
