# Win VST3 Vulkan/Skia DPI Bypass Plan

## Goal
Ensure Windows VST3 editors that render with the Skia/Vulkan backend always draw at the physical monitor DPI, independent of host-provided UI scaling hints, while keeping the plug-in window and swapchain sizes synchronized so the UI is not clipped or oversized.

## Status
- [x] Phase 1 — Investigation & Audit (see `WinVST3VulkanSkia_Audit.md`)
- [x] Phase 2 — Implementation & Integration

## Scope & Constraints
- Platform: Windows only.
- Plug-in format: VST3 only.
- Rendering path: Skia with Vulkan (GPU) only. Other renderers (NanoVG, CPU Skia, GL, D2D, etc.) remain untouched except where shared abstractions demand adjustments.
- Hosts of interest: DPI-virtualized DAWs where Steinberg's `IPlugViewContentScaleSupport` delivers logical scaling factors instead of the physical DPI.

---

## Phase 1 — Investigation & Audit
Comprehensively document the current scale negotiation, window sizing, and rendering pipeline to identify every stage influenced by host-provided DPI and the points that must be overridden.

### 1. Host ↔ View Scale Negotiation
- Trace how Steinberg hosts communicate scaling: inspect `IPlugVST3_View::setContentScaleFactor()` and any other host callbacks that adjust size or scale (e.g., `attached()`, `removed()`, resize requests).
- Confirm whether `setContentScaleFactor()` is called before or after the view receives `attached()` and how frequently hosts send updates when the monitor configuration changes.
- Determine whether hosts also modify the frame size via `CView::onSize()`/`checkSizeConstraint()` or only rely on DPI callbacks.

### 2. Editor Delegate Scale Handling
- Map how `IPlugView::setContentScaleFactor()` propagates into `IGEditorDelegate::SetScreenScale()` and `IGraphics::SetScreenScale()`.
- Record the responsibilities of `IGraphics::SetScreenScale()` (UI relayout, control rescale, draw cache flush) and identify assumptions baked into it about the source of `mScreenScale` versus `mDrawScale`.
- Verify how `IGEditorDelegate::OnParentWindowResize()` uses `GetPlatformWindowScale()` to transform host-reported pixel sizes back into logical coordinates.

### 3. Windows Windowing Layer
- Review `IGraphicsWin::OpenWindow()` and `SetScreenScale()` interactions: note when `GetScaleForHWND()` is evaluated, how the initial HWND size is computed (logical width × screen scale), and whether any feedback loops with the host exist during `OpenWindow()`.
- Audit message handling for resize/dpi events in `IGraphicsWin::WndProc` to see which notifications trigger redraws or rescale (`WM_WINDOWPOSCHANGED`, `OnDisplayTimer()` polling via `GetScaleForHWND`, missing `WM_DPICHANGED`, etc.).
- Identify how often `GetScaleForHWND()` runs (initial open, periodic checks, external calls) and what happens if it diverges from the host scale.

### 4. Vulkan Swapchain & Surface Dimensions
- Follow the Vulkan initialization in `IGraphicsWin::CreateVulkanContext()` and `CreateOrResizeVulkanSwapchain()`:
  - Determine which sizes (client rect, `VkSurfaceCapabilitiesKHR::currentExtent`, manual extents) feed into swapchain creation.
  - Investigate how `WindowWidth()`/`WindowHeight()` interplay with the swapchain dimensions when the editor resizes.
  - Confirm whether the swapchain images are in physical pixels while UI layout is in logical units.
- Inspect resize triggers such as `IGraphicsWin::OnResize()` / `RecreateVulkanContext()` to ensure swapchain rebuild aligns with window size changes.

### 5. Skia Rendering Scale Application
- Document how `IGraphicsSkia` applies scaling:
  - `GetTotalScale()` usage in matrix setup, layer transforms, and resource creation (bitmaps, shadows, etc.).
  - Any cached assumptions about `mScreenScale` versus `mDrawScale` that would break if the screen scale jumps independently of host hints.
- Check whether Skia surfaces are sized using swapchain dimensions or logical ones and how `EnsureSwapchainSurface()` maps Vulkan images to Skia canvases.

### 6. Editor Resize & Constraints
- Evaluate how `IGraphics::Resize()` interacts with `mDrawScale` and `mScreenScale` and whether resizing from the UI triggers host notifications that may reapply host scaling.
- Inspect the VST3 controller/component negotiation for initial view size (`IPlugVST3::EditorResize()`, `pluginterfaces::vst::ViewRect`) to identify potential conflicts when enforcing physical DPI.

### 7. Runtime Diagnostics & Tooling
- Identify logging facilities already present (e.g., `VulkanLogging.h`, scheduler logs) that can capture scale/extent mismatches.
- Plan temporary instrumentation (ifdef-guarded logging) to verify physical DPI values, host-provided scale factors, HWND client rects, and swapchain extents while testing.

### 8. Edge Cases & Regression Risks
- List hosts known to virtualize DPI heavily (e.g., Cubase/Nuendo, older Reaper builds) and note any quirks in how they size the view.
- Consider multi-monitor scenarios (moving the window between monitors with different DPIs), ensuring the physical DPI computation remains accurate without host cooperation.
- Check how tooltip windows, text entry HWNDs, or child platform views derive scaling so they continue to match the main canvas after bypassing host factors.

Deliverable: A detailed audit document (can extend `WinVST3VulkanSkia_CurrentState.md` or new notes) summarizing findings with references to code locations and runtime observations.

---

## Phase 2 — Implementation & Integration
Use the investigation results to enforce physical DPI usage across the Windows VST3 Skia/Vulkan pipeline, ignoring host scaling preferences while maintaining correct window/swapchain alignment.

### 1. Establish Physical DPI Source of Truth
- Refine `GetScaleForHWND()` (or introduce a new helper) to return the effective physical scaling factor per monitor, leveraging the virtual-to-physical desktop ratio fallback for DPI-virtualized hosts.
- Cache and expose this physical scale distinctly from any host-provided logical scale so downstream code can differentiate between "actual pixels" and "host hints".

### 2. Override Host Scale Inputs
- Update `IPlugVST3_View::setContentScaleFactor()` to ignore or sandbox Steinberg's factor when running on Windows with Skia/Vulkan, optionally logging discrepancies for diagnostics.
- Ensure other potential host callbacks (`checkSizeConstraint`, custom attributes) do not reintroduce logical scaling—guard or bypass them as needed.

### 3. Synchronize Window & Swapchain to Physical Pixels
- Adjust `IGraphicsWin::OpenWindow()` and resize paths so the HWND client size always matches `logicalSize × physicalScale`, regardless of host expectations, and confirm that `SetWindowPos` negotiations still succeed.
- When `GetScaleForHWND()` changes, update both the stored screen scale and trigger window/swapchain resizes so the client area and Vulkan images stay in sync.
- Integrate handling for `WM_DPICHANGED` (or equivalent) to react immediately to monitor DPI switches, using the suggested `RECT` to resize the window if necessary.

### 4. Align Editor Delegate Calculations
- Modify `IGEditorDelegate::OnParentWindowResize()` and related helpers so logical-to-physical conversions rely on the new physical scale, avoiding host-provided content scale factors.
- Review `IGraphics::SetScreenScale()` / `GetPlatformWindowScale()` to ensure the "platform scale" reported to layout code equals the physical DPI multiplier.
- Confirm `mDrawScale` (user-resizable zoom) composes cleanly with the physical scale to yield `GetTotalScale()`.

### 5. Vulkan & Skia Integration Updates
- Propagate the physical scale into swapchain extent calculations when `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` allows free sizing, ensuring we request images sized to the current physical pixel dimensions.
- Validate that Skia's `SkSurface` wrappers and `IGraphicsSkia::PathTransformSetMatrix()` use the updated `GetTotalScale()` so drawing aligns pixel-perfectly with the swapchain images.
- Audit off-screen bitmaps, layers, and cached render targets to ensure their scale parameters now reflect the physical DPI, avoiding mismatched resource sizes.

### 6. Ancillary Windows UI Elements
- Ensure tooltips (`mTooltipWnd`), parameter edit controls (`mParamEditWnd`), and any embedded platform views (WebView, etc.) also adopt the physical scale so text/input fields remain aligned.
- Update hit-testing, cursor positioning, drag-and-drop conversions, and mouse wheel math where `GetTotalScale()` or `GetPlatformWindowScale()` are used to translate between screen and logical coordinates.

### 7. Validation & Regression Testing
- Devise a manual test matrix covering: DPI-virtualized host at 150%+, high-DPI monitor with and without host virtualization, moving the editor between monitors, and resizing the plug-in UI.
- Capture logs/screenshots to verify the canvas fills the window, swapchain extents match HWND client rects, and mouse interactions line up at different scales.
- Confirm non-targeted backends (GL, CPU Skia) remain unaffected by gating changes behind appropriate compile/runtime checks.

### 8. Documentation & Clean-Up
- Update developer documentation (e.g., `WinVST3VulkanSkia_CurrentState.md` or new notes) with the new physical-DPI strategy and any host compatibility considerations.
- Remove temporary instrumentation or wrap it behind debug macros.
- Prepare migration notes for plug-in authors relying on host scaling preferences, clarifying the new behavior on Windows VST3 with Skia/Vulkan.

Deliverable: Code changes and documentation updates implementing the physical-DPI pipeline, with validation evidence that the UI renders pixel-perfectly on high-DPI Windows hosts.

