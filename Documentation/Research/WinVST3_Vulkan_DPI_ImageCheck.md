# Windows VST3 Vulkan HiDPI Image Check

Hey — here is the investigation you asked for.

## 1. How the DPI scale is acquired
- `GetScaleForHWND()` now loads `SetThreadDpiAwarenessContext` alongside `GetDpiForWindow` and temporarily switches the calling thread to per-monitor-aware v2 when the host asks for the plug-in view. This guarantees that Windows returns the real monitor DPI even if the host itself is DPI-unaware, so the draw scale we propagate is no longer stuck at 1.0 on high-density displays.【F:IPlug/IPlug_include_in_plug_src.h†L25-L82】【F:IPlug/ReaperExt/ReaperExt_include_in_plug_src.h†L120-L159】

## 2. How that scale is used to size the native window and swapchain
- When the editor opens, the Windows backend multiplies the logical editor size by the screen scale returned above before creating the child window and immediately calls `SetScreenScale()` with the same factor. That means the Vulkan swapchain is built at physical pixel dimensions that match the monitor DPI instead of a low-resolution fallback.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4205-L4273】

## 3. How Skia renders into the Vulkan images
- `IGraphicsSkia::DrawResize()` re-computes the backing surface dimensions as `WindowWidth() * GetScreenScale()` whenever the DPI changes, reconfigures the swapchain with those pixel dimensions, and rebuilds the Skia render target at the same size. No stretch or upscaling happens after this point because every Skia surface now matches the swapchain images exactly.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1306-L1487】
- During `BeginFrame()` the same physical dimensions are used when wrapping the swapchain image for drawing, so each frame is rendered at native resolution before presentation.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1588-L1659】

## 4. Conclusion
With the DPI query happening inside a per-monitor-aware scope, the draw scale fed into the Vulkan/Skia backend reflects the monitor’s true pixel density. Because every subsequent stage (window creation, swapchain sizing, and Skia surface creation) already multiplies by that scale, the rendered image now lands on the swapchain at full resolution instead of a blurry, upscaled texture.
