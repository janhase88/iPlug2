# Windows VST3 Skia/Vulkan DPI Rollout Monitoring

The physical-DPI pipeline relies on a mix of Win32 metrics and Vulkan surface checks. This guide lists the telemetry and manual signals required during rollout.

## 1. Logging Streams
| Source | Description | Notes |
| --- | --- | --- |
| `DBGMSG` / `IGRAPHICS_VK_LOG` (event: `RefreshPlatformScale`) | Emitted together for every scale refresh, capturing the measured physical scale, host DPI scale, bypass flag, and force flag. Verbose logging is forced on even for release builds.【F:IGraphics/Platforms/IGraphicsWin.cpp†L47-L62】【F:IGraphics/Platforms/IGraphicsWin.cpp†L4381-L4416】 | Capture with DebugView or a debugger. Treat repeated flips between two scales as a sign the host is fighting the bypass. |
| `IGRAPHICS_VK_LOG` (`CreateOrResizeVulkanSwapchain`) | Records surface capability ranges, whether the host `currentExtent` was bypassed, and the chosen extents for every swapchain recreation.【F:IGraphics/Platforms/IGraphicsWin.cpp†L3974-L4245】 | Alert on extents that diverge from the HWND client size or frequent recreations triggered without DPI changes. |
| `DBGMSG` / `IGRAPHICS_VK_LOG` (event: `SetHostContentScaleBypassed`) | Fires whenever the bypass flag changes, confirming whether the editor is ignoring host scale hints before the first resize.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4346-L4367】 | Use this as a sanity check in bug reports claiming the bypass never engaged. |
| `DBGMSG` / `IGRAPHICS_VK_LOG` (event: `PlatformResize`) | Logged on each platform resize, showing the window (host) scale, render (physical) scale, chosen target scale, bypass flag, and whether the parent already resized.【F:IGraphics/Platforms/IGraphicsWin.cpp†L3526-L3571】 | Use to confirm the child window stays at the host DPI (`targetScale`≈host) while the renderer keeps the physical scale. |
| Host resize traces (optional) | Some DAWs expose verbose resize logging. Enable when available to confirm the host continues to request logical pixels. | Look for hosts repeatedly sending “restore” sizes after we grow the HWND; log a bug if windows visibly fight. |

## 2. Dashboards & Alerts
- **Scale drift:** Create a simple script that parses `DBGMSG` output and raises a warning if the same editor reports two different `host` scale values within 5 seconds while the physical scale remains constant. This highlights hosts that keep toggling virtualization states.
- **Swapchain churn:** Track the number of swapchain recreations per minute. Flag sessions exceeding 4 recreations/minute when no DPI change occurred—this usually indicates a host resizing loop.
- **Mixed-backend regression:** Compare the presence of `DBGMSG` entries between Vulkan/Skia builds and CPU/NanoVG builds. The bypass log should be absent from non-targeted renderers, confirming the gating is correct.【F:IPlug/VST3/IPlugVST3_View.h†L99-L132】

## 3. Runbook Actions
1. **Blurry rendering reports**
   - Pull the latest `DBGMSG` scale pairs; if physical and host values match, the bypass may not have engaged. Verify the build is Vulkan/Skia and the bypass flag was set during `attached()`.【F:IPlug/VST3/IPlugVST3_View.h†L101-L136】
   - If physical ≈ host, instruct users to confirm they are running a Vulkan/Skia configuration and not a CPU fallback.
2. **Window clipping or oversize**
   - Review recent swapchain logs for extents; ensure they match the measured physical size (physical scale × logical size).【F:IGraphics/Platforms/IGraphicsWin.cpp†L3823-L4098】
   - Check the paired `RefreshPlatformScale` events for disparities between `measured` and `host`; a large gap with no window resize implies the host is still virtualizing UI size.
   - If extents match but the host keeps shrinking the parent window, capture host resize logs and consider adding a per-host compatibility toggle to disable the bypass.
3. **Multi-monitor glitches**
   - Confirm a `WM_DPICHANGED` log entry followed the move. If not, reproduce with the plugin owning mouse capture; the fix may require synthetic scale checks while captured.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2682-L2707】

## 4. Rollback Criteria
- Physical vs. host scale equality in logs for DPI-virtualized hosts (Cubase, Studio One) despite the bypass being enabled.
- Persistent swapchain recreations where extents never stabilise, causing flicker or GPU stalls.
- Reports of input misalignment that reproduce with `GetTotalScale()` conversions verified in code, indicating a deeper Win32 DPI bug.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2590-L2679】【F:IGraphics/IGraphics.h†L1133-L1150】
