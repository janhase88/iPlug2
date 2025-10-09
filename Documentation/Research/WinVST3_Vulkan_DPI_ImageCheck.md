# Windows VST3 Vulkan HiDPI Image Check

Hey — here is the investigation you asked for.

## 1. How the DPI scale is acquired
- `GetScaleForHWND()` now loads `SetThreadDpiAwarenessContext` alongside `GetDpiForWindow` and temporarily switches the calling thread to per-monitor-aware v2 when the host asks for the plug-in view. This guarantees that Windows returns the real monitor DPI even if the host itself is DPI-unaware, so the draw scale we propagate is no longer stuck at 1.0 on high-density displays.【F:IPlug/IPlug_include_in_plug_src.h†L25-L82】【F:IPlug/ReaperExt/ReaperExt_include_in_plug_src.h†L120-L159】

## 2. How that scale is used to size the native window and swapchain
- When the editor opens, the Windows backend multiplies the logical editor size by the screen scale returned above before creating the child window and immediately calls `SetScreenScale()` with the same factor. That means the Vulkan swapchain is built at physical pixel dimensions that match the monitor DPI instead of a low-resolution fallback.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4205-L4273】

## 3. How Skia renders into the Vulkan images
- `IGraphicsSkia::DrawResize()` re-computes the backing surface dimensions as `WindowWidth() * GetScreenScale()` whenever the DPI changes, reconfigures the swapchain with those pixel dimensions, and rebuilds the Skia render target at the same size. No stretch or upscaling happens after this point because every Skia surface now matches the swapchain images exactly.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1306-L1487】
- During `BeginFrame()` the same physical dimensions are used when wrapping the swapchain image for drawing, so each frame is rendered at native resolution before presentation.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1588-L1659】

## 4. Are layers being generated at the correct size?
You asked whether an off-screen layer could still be rendered too small and then stretched, causing the blur you see. The layer allocation path multiplies the logical bounds by the full backing pixel scale (`screenScale × drawScale`) before allocating the bitmap, so each layer surface already matches the monitor DPI. The same scale feeds into the Skia transform so that any drawing operations land at those physical pixels without a post-pass stretch.【F:IGraphics/IGraphics.cpp†L2144-L2152】【F:IGraphics/IGraphics.h†L1848-L1848】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1306-L1344】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L2734-L2758】

- `IGraphics::StartLayer()` snaps the requested bounds to pixel coordinates and multiplies by `GetBackingPixelScale()` before calling `CreateAPIBitmap`, ensuring the surface resolution scales with DPI.
- `GetBackingPixelScale()` itself returns `GetScreenScale() * GetDrawScale()`, so any content-scale factor from the host and any user draw-scale preference are both baked into that layer size.
- `IGraphicsSkia::DrawResize()` rebuilds the swapchain and Skia render target using `WindowWidth() * GetScreenScale()` and `WindowHeight() * GetScreenScale()`, so the backing store that layers eventually composite onto matches those DPI-correct layer dimensions.
- `IGraphicsSkia::PathTransformSetMatrix()` composes the layer translation with the global `GetTotalScale()` matrix, so Skia draws into the layer with the same physical scaling it used to allocate the surface.

Because the layer bitmaps, the swapchain images, and the Skia canvas all agree on the physical pixel size, there isn’t an extra upscaling step inside the rendering stack. If the output is still blurry, we need to keep tracking where the screen scale is getting quantized or overridden before it reaches this layer path.

## 5. Conclusion
With the DPI query happening inside a per-monitor-aware scope, the draw scale fed into the Vulkan/Skia backend reflects the monitor’s true pixel density. Because every subsequent stage (window creation, swapchain sizing, layer creation, and Skia surface transforms) already multiplies by that scale, the rendered image now lands on the swapchain at full resolution instead of a blurry, upscaled texture. The remaining blur has to come from the scale factor being wrong before it reaches these code paths, not from layers being created at the wrong size.
