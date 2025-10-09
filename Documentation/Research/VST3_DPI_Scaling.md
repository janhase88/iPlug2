# VST3 HiDPI Content Scaling Research

## Steinberg VST3 documentation

> "This interface communicates the content scale factor from the host to the plug-in view on systems where plug-ins cannot get this information directly like Microsoft Windows. The host calls `setContentScaleFactor` directly before or after the plug-in view is attached and when the scale factor changes while the view is attached (system change or window moved to another screen with different scaling settings). The host may call `setContentScaleFactor` in a different context, for example: scaling the plug-in editor for better readability. When a plug-in handles this (by returning `kResultTrue`), it needs to scale the width and height of its view by the scale factor and inform the host via a `IPlugFrame::resizeView()`. The host will then call `IPlugView::onSize()`. Note that the host is allowed to call `setContentScaleFactor()` at any time the `IPlugView` is valid. If this happens before the `IPlugFrame` object is set on your view, make sure that when the host calls `IPlugView::getSize()` afterwards you return the size of your view for that new scale factor. It is recommended to implement this interface on Microsoft Windows to let the host know that the plug-in is able to render in different scalings." — Steinberg VST3 SDK `IPlugViewContentScaleSupport` documentation.【F:Documentation/Research/VST3_DPI_Scaling.md†L4-L10】

> "Called to inform the host about the resize of a given view. Afterwards the host has to call `IPlugView::onSize()`." — Steinberg VST3 SDK `IPlugFrame::resizeView` documentation.【F:Documentation/Research/VST3_DPI_Scaling.md†L12-L13】

## iPlug2 integration touch points

- `IPlugVST3View` implements `Steinberg::IPlugViewContentScaleSupport` and forwards `setContentScaleFactor` to the editor delegate, so the class is the entry point for host-provided DPI factors on the VST3 path.【F:IPlug/VST3/IPlugVST3_View.h†L21-L146】
- `IPlugVST3View::Resize` wraps the host callback `plugFrame->resizeView(this, &newSize)` so that iPlug informs the host after adjusting its dimensions for the requested scale.【F:IPlug/VST3/IPlugVST3_View.h†L338-L344】
- `IGEditorDelegate::SetScreenScale` calls into `IGraphics::SetScreenScale`, which updates the UI’s internal draw scale, resizes the underlying window, and triggers control rescaling.【F:IGraphics/IGraphicsEditorDelegate.cpp†L60-L75】【F:IGraphics/IGraphics.cpp†L70-L101】
- When a VST3 editor asks for a resize, `IPlugVST3::EditorResize` ensures `IPlugVST3View::Resize` is called so the host receives the new dimensions through `IPlugFrame::resizeView`.【F:IPlug/VST3/IPlugVST3.cpp†L214-L233】
- On Windows, `IGraphicsWin::OpenWindow` reads the monitor scaling factor via `GetScaleForHWND`, sizes the native child window in physical pixels, and immediately invokes `SetScreenScale` so the Vulkan/Skia backend is created at the right resolution before the first frame.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4210-L4272】

## Implementation implications

- Because the host may call `setContentScaleFactor` before the window is attached, the plug-in should cache the scale factor and ensure `IPlugView::getSize()` returns logical dimensions that already include the pending scale before the first `onSize` arrives.【F:Documentation/Research/VST3_DPI_Scaling.md†L4-L10】【F:IPlug/VST3/IPlugVST3_View.h†L50-L113】
- Hosts can expose user-facing toggles to force or disable high-DPI drawing. A compliant plug-in must respect those requests by updating its view size and calling back through `IPlugFrame::resizeView`, letting the host drive the final swapchain or backing store size.【F:Documentation/Research/VST3_DPI_Scaling.md†L4-L13】【F:IPlug/VST3/IPlugVST3_View.h†L338-L344】【F:IGraphics/IGraphics.cpp†L70-L101】
- The Windows backend derives its swapchain size from `SetScreenScale`, so any DPI handling bug is likely rooted in the flow from `IPlugViewContentScaleSupport::setContentScaleFactor` through `IGraphics::SetScreenScale` rather than in unrelated platform helpers.【F:IPlug/VST3/IPlugVST3_View.h†L142-L146】【F:IGraphics/IGraphicsEditorDelegate.cpp†L60-L75】【F:IGraphics/IGraphics.cpp†L70-L101】【F:IGraphics/Platforms/IGraphicsWin.cpp†L4210-L4272】
