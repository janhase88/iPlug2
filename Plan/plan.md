# Windows VST3 Skia/Vulkan DPI Plan

## Phase 1 – Investigation ✅
- Confirmed the host was still feeding the virtual 96 DPI scale when DPI virtualization is active.
- Identified the monitor's physical pixel size via `EnumDisplaySettingsW` so we can derive the virtualization ratio.

## Phase 2 – Instrumentation ✅
- [x] Emit `DBGMSG` telemetry from `GetScaleForHWND`/`IGraphicsWin` so every DPI query shows host DPI, virtualization ratio, and effective scale.
- [x] Add Skia bitmap/surface diagnostics to track logical vs render pixels for each swapchain image.
- [x] Promote the Vulkan logger to verbose mode and annotate swapchain extent selection so we can see when caps clamp the render target.

## Phase 3 – Validation 🚧
- [ ] Run a Windows VST3 build at 100 % and 150 % scale, capture the new backend/Skia/Vulkan logs, and confirm the swapchain extent matches the physical pixels now that host vs. render DPI are separated.
- [ ] If the swapchain still reports the virtual extent, trace through the captured logs to spot which stage falls back and decide on the next corrective action.
