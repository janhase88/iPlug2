# Windows VST3 Skia/Vulkan DPI Audit

## Host ↔ View Scale Negotiation
- `IPlugVST3_View::setContentScaleFactor()` forwards Steinberg's logical content scale to `IGEditorDelegate::SetScreenScale`, which in turn calls `IGraphics::SetScreenScale`. (File: `IPlug/VST3/IPlugVST3_View.h`).
- `IPlugVST3_View::onSize()` delivers the host-provided `ViewRect` to `IGEditorDelegate::OnParentWindowResize`, so width/height changes from the DAW are interpreted relative to the current platform scale.
- The editor view advertises `IPlugViewContentScaleSupport`, so hosts that virtualise DPI actively push logical factors via `setContentScaleFactor()`.

## Editor Delegate Scale Handling
- `IGEditorDelegate::SetScreenScale()` simply forwards the factor to `IGraphics::SetScreenScale()` without differentiating between host hints and physical monitor DPI (`IGraphics/IGraphicsEditorDelegate.cpp`).
- `IGraphics::SetScreenScale()` stores the new `mScreenScale`, recalculates the platform window size (`WindowWidth() * GetPlatformWindowScale()`), notifies the host through `EditorResizeFromUI()`, and then triggers platform-specific resizing and relayout (`IGraphics/IGraphics.cpp`).
- `IGEditorDelegate::OnParentWindowResize()` divides the incoming host width/height by `GetPlatformWindowScale()` (which resolves to the cached host DPI on Windows) and by `GetDrawScale()` to recover logical UI coordinates.

## Windows Windowing Layer
- `IGraphicsWin::OpenWindow()` calculates an initial client size by multiplying the logical editor dimensions with the measured host DPI (falling back to `GetScaleForHWND()` when the host scale is unavailable) before creating the plug-in child window (`IGraphics/Platforms/IGraphicsWin.cpp`).
- `IGraphicsWin::PlatformResize()` compares the desired pixel dimensions (`WindowWidth() * GetScreenScale()`) with the actual HWND client rect, issuing `SetWindowPos()` calls to adjust the plug-in window and, if required, parent shells.
- `IGraphicsWin::OnDisplayTimer()` polls `GetScaleForHWND(mPlugWnd)` on every idle tick (when the plug-in does not own the mouse capture) and calls `SetScreenScale()` whenever the observed DPI differs.

## Vulkan Swapchain & Surface Dimensions
- During `OpenWindow()`, `CreateVulkanContext()` builds the initial swapchain using `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`, seeding `CreateOrResizeVulkanSwapchain()` with `caps.currentExtent.width/height` (`IGraphics/Platforms/IGraphicsWin.cpp`).
- `IGraphicsSkia::DrawResize()` computes the render target size as `ceil(WindowWidth() * GetScreenScale())` before requesting a swapchain resize; `CreateOrResizeVulkanSwapchain()` clamps the requested extent to the surface capabilities and rebuilds the swapchain images (`IGraphics/Drawing/IGraphicsSkia.cpp`).
- Swapchain recreation is also triggered by `IGraphicsWin::RecreateVulkanContext()` during VBlank health resets or when `DrawResize()` detects stale images.

## Skia Rendering Scale Application
- Skia drawing primitives consistently call `WindowWidth() * GetScreenScale()` (or `GetTotalScale()`) when sizing surfaces (`IGraphics/Drawing/IGraphicsSkia.cpp`), so cached layers and render targets follow the platform scale tracked by `IGraphics`.
- `IGraphicsSkia::BeginFrame()` and `EndFrame()` work with swapchain-backed `SkSurface` objects whose extents match the Vulkan images requested during `DrawResize()`.

## Editor Resize & Constraints
- Host-originated resizes flow through `IPlugVST3_View::onSize()` → `IGEditorDelegate::OnParentWindowResize()` → `IGraphics::Resize()`. The logical target size is derived from host width/height divided by `(screenScale * drawScale)` before controls are relaid out (`IGraphics/IGraphicsEditorDelegate.cpp`).
- `IGraphics::Resize()` then notifies the host again via `EditorResizeFromUI()` using `WindowWidth() * GetPlatformWindowScale()`, meaning the size fed back to the host is already multiplied by the active platform scale.

## Runtime Diagnostics & Tooling
- Scheduler/VBlank telemetry already records swapchain recreations, surface extents, and DPI-related events through the `vulkanlog` helpers (`IGraphics/Drawing/IGraphicsSkia.cpp` & `IGraphics/Platforms/IGraphicsWin.cpp`).
- Temporary instrumentation can piggyback on existing logging macros such as `IGRAPHICS_VK_LOG` or `schedulerlog::LogEvent` without introducing new facilities.

## Edge Cases & Regression Risks
- Hosts that virtualise DPI (e.g., Cubase, Studio One) rely on `setContentScaleFactor()` to drive scaling, so bypassing the host factor requires the plug-in to derive physical DPI from Win32 APIs alone.
- Moving the plug-in window between monitors currently depends on the periodic `GetScaleForHWND()` polling; there is no explicit `WM_DPICHANGED` handler, so DPI changes while the window owns capture can be missed.
- Auxiliary HWNDs (tooltip and parameter edit windows) scale using `GetTotalScale()`, so they remain consistent provided `mScreenScale` tracks the physical DPI.

## Summary of Required Changes
1. Promote a single "physical scale" source of truth in `IGraphicsWin`, refreshing it directly from the HWND DPI metrics (and reacting to `WM_DPICHANGED`).
2. Ignore Steinberg's `setContentScaleFactor()` on Windows Skia/Vulkan builds so host-provided logical scaling hints cannot override the measured physical DPI.
3. Ensure all swapchain and surface sizing routines consume the refreshed physical scale by keeping `WindowWidth()` logical and multiplying by the tracked physical scale within `DrawResize()` and related helpers.
4. Guard ancillary UI elements (text entry, tooltips, mouse conversions) behind `GetTotalScale()` so they follow the new physical scale automatically.
