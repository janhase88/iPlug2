# Windows VST3 Skia/Vulkan DPI Plan

## Phase 1 – Investigation ✅
- Confirmed the host was still feeding the virtual 96 DPI scale when DPI virtualization is active.
- Identified the monitor's physical pixel size via `EnumDisplaySettingsW` so we can derive the virtualization ratio.

## Phase 2 – Implementation 🚧
- [x] Multiply `GetDpiForWindow` by the physical/virtual pixel ratio for Windows editors and the Reaper extension.
- [ ] Rebuild and inspect the Vulkan logs to ensure the reported screen scale now reflects the monitor DPI (e.g. 1.5 at 150 %).

## Phase 3 – Validation ⏳
- [ ] Capture screenshots/logs at 100 % and 150 % scaling to confirm the swapchain picks the physical pixels and the UI renders crisp.
- [ ] Decide on any follow-up instrumentation or manual DPI overrides if the logs still read 1.0.
