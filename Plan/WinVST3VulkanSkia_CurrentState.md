# Windows VST3 Skia/Vulkan DPI Pipeline — Current State

## Steinberg Host Integration
- `IPlugVST3View` exposes `IPlugViewContentScaleSupport` but bypasses the host-provided scale factor on Windows when both Skia and Vulkan are enabled, so the editor never reuses Steinberg's logical DPI hints.【F:IPlug/VST3/IPlugVST3_View.h†L41-L146】
- During `attached()`, the view opens the platform window and immediately enables the bypass flag on the `IGraphics` instance, ensuring the native layer opts out of host DPI virtualization before any WM messages arrive.【F:IPlug/VST3/IPlugVST3_View.h†L99-L132】

## Editor Delegate Behaviour
- `IGEditorDelegate::OnParentWindowResize()` converts host resize messages from HWND pixels back into logical UI units using `GetBackingPixelScaleForParentResize()`, keeping draw-scale zoom and layout math in logical space while honoring the bypass-aware platform conversion.【F:IGraphics/IGraphicsEditorDelegate.cpp†L58-L102】
- The delegate caches the last logical size and draw scale so reopening an editor restores the same logical dimensions regardless of the monitor DPI that was active when it last closed.【F:IGraphics/IGraphicsEditorDelegate.cpp†L23-L56】

## Measuring Physical DPI
- `GetScaleForHWND()` resolves per-window DPI via `GetDpiForWindow` and multiplies it by the physical-to-virtual desktop width ratio when the host is DPI virtualized, yielding the physical pixel scale even when the DAW only exposes logical coordinates.【F:IPlug/IPlug_include_in_plug_src.h†L29-L78】
- `IGraphicsWin::RefreshPlatformScale()` caches both the measured physical scale and the host-facing DPI value, logging the update and pushing the new screen scale into the graphics layer so Skia and Vulkan surfaces redraw at the monitor's physical pixels.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4270-L4346】
- The helper records `mWindowDPIScale` separately from `mScreenScale`, allowing host-space conversions to stay stable while the renderer consumes the physical scale.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4270-L4346】【F:IGraphics/Platforms/IGraphicsWin.h†L91-L107】

## Window Resizing and DPI Events
- `PlatformResize()` compares the desired physical pixel size (`WindowWidth() * GetScreenScale()`) with the HWND client rect and forces both the plug-in window and parent shells to grow when the bypass is active, keeping the child window aligned with the physical swapchain dimensions.【F:IGraphics/Platforms/IGraphicsWin.cpp†L3526-L3562】
- `WM_DPICHANGED` messages apply the suggested rectangle, refresh the platform scale immediately, and trigger a Skia/Vulkan relayout so moving between monitors adopts the new physical DPI without waiting for idle polling.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2682-L2707】

## Swapchain and Skia Surface Sizing
- `OpenWindow()` initializes Vulkan, then calls `RefreshPlatformScale(true)` before laying out controls, ensuring the initial swapchain and Skia surfaces use the measured physical DPI even on the first frame.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4348-L4412】
- `CreateVulkanContext()` pulls `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`, then delegates to `CreateOrResizeVulkanSwapchain()`, which clamps requested extents to the surface limits and rebuilds swapchain images whenever the window size changes.【F:IGraphics/Platforms/IGraphicsWin.cpp†L3823-L4098】
- Skia's `DrawResize()` (invoked via `RefreshPlatformScale()` and `Resize()`) recalculates the render target size from `WindowWidth() * GetScreenScale()`, so off-screen layers and swapchain-backed surfaces stay matched to the physical DPI.【F:IGraphics/IGraphics.cpp†L70-L120】

## Input, Tooltips, and Auxiliary Windows
- Mouse-wheel, touch, and cursor APIs translate between screen pixels and logical coordinates using `GetTotalScale()`, ensuring hit-testing remains accurate when the physical DPI diverges from the host's logical scale.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2590-L2679】【F:IGraphics/IGraphics.h†L1133-L1150】
- Tooltip and parameter edit HWNDs are re-created after `OpenWindow()` while the physical scale is active, so their fonts and bounds follow the same pixel density as the main canvas.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4414-L4499】

## Diagnostics and Logging
- Every platform-scale refresh now emits a `DBGMSG` with the measured physical scale, host DPI scale, and bypass state. These messages provide lightweight telemetry confirming that DPI virtualization is being ignored and help correlate host resize issues with the underlying scale values.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4327-L4334】
- Vulkan swapchain helpers already log surface capabilities and extent selections through `IGRAPHICS_VK_LOG`, so DPI-driven resize churn can be reviewed without additional instrumentation.【F:IGraphics/Platforms/IGraphicsWin.cpp†L3974-L4195】
