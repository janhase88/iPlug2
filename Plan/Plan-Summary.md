[x] Continued From Previous Snapshot: YES — Continuing the Windows VST3 Skia/Vulkan DPI investigation with host/physical scale separation.
-----------------
[x] File Overview:
- Num files Changed: 2
- Num files Created: 0
- Num lines Modified: <200 (Vulkan renderpass instrumentation + status docs)

[x] List of Files changed/created:
- Changed:
  - IGraphics/Drawing/IGraphicsSkia.cpp
  - AgentChat.md

-----------------
[x] Current plan:
- DPI Investigation — PREVIOUS STATUS: IMPLEMENTATION / CURRENT STATUS: IN PROGRESS
  - Step 1: Verify Windows virtualization ratio logging — PREVIOUS STATUS: UPDATED / CURRENT STATUS: COMPLETE (logs now emit host/effective/raw DPI; latest run shows Windows not virtualizing this HWND).
- Step 2: Force Skia DPR to 1.5× and capture crispness result — PREVIOUS STATUS: COMPLETE / CURRENT STATUS: COMPLETE (forced DPR runs remain blurry, confirming the issue lies beyond Skia’s DPR setting).
  - Step 3: Inspect Vulkan renderpass / cached surfaces — PREVIOUS STATUS: IN PROGRESS / CURRENT STATUS: IN PROGRESS (fresh instrumentation logs `RenderPass.*` snapshots for swapchain surfaces, cached layers, and flush/present transitions so we can pinpoint where resolution collapses).

[x] Message to User:
Please rebuild and rerun the 150 % test case. The new build emits `RenderPass.*` log entries from `EnsureSwapchainSurface`, `BeginFrame`, `PrepareCurrentSwapchainImageForFlush`, `EndFrame`, and GPU layer creation—each snapshot should show 1800×750, sampleCnt, and layout data. Share that output (plus the on-screen result) so we can see whether the renderpass or cached surfaces are dropping resolution.
