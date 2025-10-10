# Windows VST3 Skia/Vulkan DPI Final Review (Prep Notes)

## Scope
This document captures the checkpoints that must pass before declaring the Windows VST3 Skia/Vulkan physical-DPI work complete. It will be expanded into a full retrospective once manual validation wraps.

## Items Ready for Review
- **Physical scale enforcement** – Host scaling hints are ignored on Windows/Skia/Vulkan builds and the renderer consumes the measured monitor DPI through `RefreshPlatformScale()` with explicit bypass toggles logged for auditors.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4270-L4345】
- **Swapchain alignment** – `CreateOrResizeVulkanSwapchain()` records the chosen extents and whether the host `currentExtent` was bypassed, enabling reviewers to confirm Vulkan surfaces match the HWND size during validation runs.【F:IGraphics/Platforms/IGraphicsWin.cpp†L3823-L4245】
- **Documentation & tooling** – Current-state analysis, validation checklist, and rollout monitoring guides are updated to describe the DPI bypass and the evidence expected from manual testers.【F:Plan/WinVST3VulkanSkia_CurrentState.md†L1-L40】【F:Plan/WinVST3VulkanSkia_ValidationChecklist.md†L1-L58】【F:Plan/WinVST3VulkanSkia_RolloutMonitoring.md†L1-L52】

## Outstanding Before Sign-off
1. Execute the validation checklist across at least one DPI-virtualized host and one per-monitor aware host. Collect the required logs/screenshots and confirm crisp rendering throughout.
2. Decide whether the new `DBGMSG` scale logging should be permanently enabled, gated behind a compile flag, or promoted to structured logging.
3. Draft migration guidance for plug-in developers explaining the always-physical DPI behaviour and any host-specific caveats.

## Next Actions
Once the above items are complete, consolidate findings, attach captured evidence, and update this document with the final review summary and go/no-go recommendation.
