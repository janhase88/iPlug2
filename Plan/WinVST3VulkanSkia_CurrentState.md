# Windows VST3 Skia/Vulkan DPI Pipeline — Current State

## Steinberg Host Integration
- `IPlugVST3View` exposes `IPlugViewContentScaleSupport` but bypasses the host-provided scale factor on Windows when both Skia and Vulkan are enabled, so the editor never reuses Steinberg's logical DPI hints.【F:IPlug/VST3/IPlugVST3_View.h†L41-L147】
- During `attached()`, the view opens the platform window and immediately toggles the bypass flag on the `IGraphics` instance; the forced refresh that follows logs `bypass=true` and, if the open fails, the flag is reset to `false`.【F:IPlug/VST3/IPlugVST3_View.h†L101-L136】【F:IGraphics/Platforms/IGraphicsWin.cpp†L4346-L4367】

## Editor Delegate Behaviour
- `IGEditorDelegate::OnParentWindowResize()` converts host resize messages from HWND pixels back into logical UI units using `GetBackingPixelScaleForParentResize()`, keeping draw-scale zoom and layout math in logical space while honoring the bypass-aware platform conversion.【F:IGraphics/IGraphicsEditorDelegate.cpp†L58-L102】
- The delegate caches the last logical size and draw scale so reopening an editor restores the same logical dimensions regardless of the monitor DPI that was active when it last closed.【F:IGraphics/IGraphicsEditorDelegate.cpp†L23-L56】

## Measuring Physical DPI
- `GetScaleForHWND()` resolves per-window DPI via `GetDpiForWindow` and multiplies it by the physical-to-virtual desktop width ratio when the host is DPI virtualized, yielding the physical pixel scale even when the DAW only exposes logical coordinates.【F:IPlug/IPlug_include_in_plug_src.h†L29-L78】
- `IGraphicsWin::RefreshPlatformScale()` caches both the measured physical scale and the host-facing DPI value, logging the update and pushing the new screen scale into the graphics layer so Skia and Vulkan surfaces redraw at the monitor's physical pixels.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4381-L4416】
- The helper records `mWindowDPIScale` separately from `mScreenScale`, allowing host-space conversions to stay stable while the renderer consumes the physical scale.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4381-L4416】【F:IGraphics/Platforms/IGraphicsWin.h†L91-L107】

## Window Resizing and DPI Events
- `PlatformResize()` keeps host callbacks in logical pixels via `GetBackingPixelScaleForParentResize()` while sizing the HWND hierarchy with the host DPI when the bypass is active, ensuring the parent window stays in virtual pixels while the renderer still tracks the physical scale; each resize logs the window/renderer/target scales for validation.【F:IGraphics/Platforms/IGraphicsWin.cpp†L3526-L3571】
- `WM_DPICHANGED` messages apply the suggested rectangle, refresh the platform scale immediately, and trigger a Skia/Vulkan relayout so moving between monitors adopts the new physical DPI without waiting for idle polling.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2682-L2707】

## Swapchain and Skia Surface Sizing
- `OpenWindow()` initializes Vulkan, then calls `RefreshPlatformScale(true)` before laying out controls, ensuring the initial swapchain and Skia surfaces use the measured physical DPI even on the first frame.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4420-L4505】
- `CreateVulkanContext()` pulls `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`, then delegates to `CreateOrResizeVulkanSwapchain()`, which now ignores `currentExtent` whenever the bypass is active so Vulkan images are sized to the physical pixel dimensions rather than the host's virtualized width/height.【F:IGraphics/Platforms/IGraphicsWin.cpp†L3823-L4239】
- Skia's `DrawResize()` (invoked via `RefreshPlatformScale()` and `Resize()`) recalculates the render target size from `WindowWidth() * GetScreenScale()`, and likewise bypasses the host's `currentExtent` when the bypass flag is set so cached surfaces stay aligned with the physical DPI.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1346-L1478】

## Input, Tooltips, and Auxiliary Windows
- Mouse-wheel, touch, and cursor APIs translate between screen pixels and logical coordinates using `GetTotalScale()`, ensuring hit-testing remains accurate when the physical DPI diverges from the host's logical scale.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2590-L2679】【F:IGraphics/IGraphics.h†L1133-L1150】
- Tooltip and parameter edit HWNDs are re-created after `OpenWindow()` while the physical scale is active, so their fonts and bounds follow the same pixel density as the main canvas.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4414-L4499】

## Diagnostics and Logging
- Every platform-scale refresh now emits both a `DBGMSG` and an `IGRAPHICS_VK_LOG` info event with the measured physical scale, host DPI scale, bypass state, and whether the refresh was forced. Logging stays enabled even in release builds by forcing `DBGMSG` on and setting the Vulkan logger verbosity to verbose.【F:IGraphics/Platforms/IGraphicsWin.cpp†L47-L62】【F:IGraphics/Platforms/IGraphicsWin.cpp†L4381-L4416】
- `SetHostContentScaleBypassed()` logs its state transition and records a matching Vulkan event, giving immediate confirmation that the bypass engaged before the first scale refresh.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4346-L4367】
- Vulkan swapchain helpers now record whether the virtualized `currentExtent` was bypassed alongside the chosen dimensions, simplifying DPI validation from logs alone.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1346-L1478】【F:IGraphics/Platforms/IGraphicsWin.cpp†L4015-L4245】
