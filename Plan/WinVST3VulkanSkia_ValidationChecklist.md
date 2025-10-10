# Windows VST3 Skia/Vulkan DPI Validation Checklist

This checklist verifies that Windows VST3 editors rendered with the Skia/Vulkan backend always draw at the physical monitor DPI while maintaining correct host negotiations.

## 1. Environment Preparation
- [ ] Install DAWs that exercise Steinberg DPI virtualization (Cubase/Nuendo, Studio One) and a per-monitor aware host (Reaper) on Windows 10/11.
- [ ] Configure at least two monitors: one at 100 % scaling and another at ≥150 % scaling. Enable “Let Windows try to fix apps so they’re not blurry” so virtualization paths activate.
- [ ] Prepare a Vulkan/Skia build of a sample plug-in (e.g. `Examples/IPlugEffect`):
  - [ ] In `Examples/IPlugEffect/config/IPlugEffect-win.props`, change `IGRAPHICS_NANOVG;IGRAPHICS_GL2` to `IGRAPHICS_SKIA;IGRAPHICS_VULKAN` (mirror the CI script behaviour).【F:Scripts/ci/build_project-win.yml†L26-L35】
  - [ ] Open `Examples/IPlugEffect/IPlugEffect.sln` in Visual Studio 2022, select the `IPlugEffect-vst3` target, and build the x64 configuration.
  - [ ] Copy the generated `.vst3` bundle from `Examples/IPlugEffect/build-win/` into your VST3 plug-in folder.
- [ ] Launch DebugView (or attach a debugger) so the always-on `DBGMSG`/`IGRAPHICS_VK_LOG` entries from `RefreshPlatformScale()` are captured during the run.【F:IGraphics/Platforms/IGraphicsWin.cpp†L47-L62】【F:IGraphics/Platforms/IGraphicsWin.cpp†L4381-L4416】

## 2. Baseline Attachment
- [ ] Launch each host at 150 % scaling and open the plug-in editor. Confirm the `SetHostContentScaleBypassed` log flips to `true` with `mixedDpi=true`, then verify `PlatformResize` keeps the window scale at the host value while the virtualization ratio (`render/host`) matches the expected DPI (e.g. 1.5). The follow-up `applied` log should show the HWND stayed at the logical size, while `DrawResize`/`SwapchainExtent` report physical dimensions (1200×500 logical → 1800×750 render).【F:IGraphics/Platforms/IGraphicsWin.cpp†L3554-L3771】【F:IGraphics/Platforms/IGraphicsWin.cpp†L4346-L4368】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1316-L1478】
- [ ] Capture a screenshot showing the UI is crisp (no bitmap stretching) and the plug-in window bounds match the rendered content.

## 3. Host Resize Negotiation
- [ ] Drag the host’s resize handle (if available) or trigger `IPlugView::checkSizeConstraint()` to request a new editor size. Verify the editor redraws sharply with no clipping and the logged scale values remain unchanged during the resize.【F:IGraphics/IGraphicsEditorDelegate.cpp†L58-L102】【F:IGraphics/Platforms/IGraphicsWin.cpp†L3526-L3562】
- [ ] Use any in-plugin resizer (corner drag) to change the logical size. Ensure the host window reports logical (host-space) dimensions while the debug log shows the same bypassed scale.

## 4. Monitor Handover
- [ ] Move the host window between the 100 % and ≥150 % monitors. Confirm `WM_DPICHANGED` fires (scale log updates immediately) and the swapchain redraws without blurring or letterboxing.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2682-L2707】【F:IGraphics/Platforms/IGraphicsWin.cpp†L4420-L4505】
- [ ] While the window straddles both monitors, ensure the measured scale matches the monitor containing the majority of the window.

## 5. Swapchain Stability
- [ ] With the editor on the high-DPI monitor, spam rapid host resize operations. Inspect Vulkan logs to ensure `CreateOrResizeVulkanSwapchain()` selects extents equal to logical size × physical DPI and report `bypassVirtualExtent=true` while the bypass is engaged.【F:IGraphics/Platforms/IGraphicsWin.cpp†L3823-L4245】
- [ ] Confirm no unexpected swapchain recreations occur when the host sends repeated logical size notifications without DPI changes.

## 6. Ancillary UI Elements
- [ ] Open parameter edit boxes and tooltips on the high-DPI monitor. Verify text renders sharply and aligns with controls, confirming auxiliary HWNDs adopted the physical scale.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4414-L4499】
- [ ] Check mouse wheel, drag, and touch interactions land on the correct controls at both DPI settings, demonstrating consistent input scaling.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2590-L2679】

## 7. Regression Sweep
- [ ] Repeat baseline attachment with the plug-in forced to CPU Skia or NanoVG to confirm the bypass flag is not activated and the host DPI scale matches the physical scale (serves as control group).【F:IPlug/VST3/IPlugVST3_View.h†L101-L136】
- [ ] Run a legacy host that never calls `setContentScaleFactor()` to ensure the plugin still measures a physical scale and renders correctly.

## 8. Reporting
For each host/monitor combination attach:
- The `DBGMSG` excerpt showing physical vs. host DPI scales.
- Vulkan log snippets covering any swapchain resize.
- Screenshots demonstrating crisp rendering at both DPI settings.
