[x] Continued From Previous Snapshot: YES — Continuing the Windows VST3 Skia/Vulkan DPI investigation with host/physical scale separation.
-----------------
- Num files Changed: 4
- Num files Created: 0
- Num lines Modified: <200 (Vulkan renderpass instrumentation + status docs)

[x] List of Files changed/created:
- Changed:
  - AgentChat.md
  - IGraphics/Platforms/VulkanLogging.h
  - Plan/Plan-Summary.md
  - common-win.props

-----------------
[x] Current plan:
- DPI Investigation — PREVIOUS STATUS: IMPLEMENTATION / CURRENT STATUS: IN PROGRESS
  - Step 1: Verify Windows virtualization ratio logging — PREVIOUS STATUS: UPDATED / CURRENT STATUS: COMPLETE (logs now emit host/effective/raw DPI; latest run shows Windows not virtualizing this HWND).
- Step 2: Force Skia DPR to 1.5× and capture crispness result — PREVIOUS STATUS: COMPLETE / CURRENT STATUS: COMPLETE (forced DPR runs remain blurry, confirming the issue lies beyond Skia’s DPR setting).
- Step 3: Inspect Vulkan renderpass / cached surfaces — PREVIOUS STATUS: IN PROGRESS / CURRENT STATUS: IN PROGRESS (fresh instrumentation logs `RenderPass.*` snapshots for swapchain surfaces, cached layers, and flush/present transitions so we can pinpoint where resolution collapses, and Vulkan log verbosity is now forced to verbose so those events appear without extra setup).

[x] Message to User:
Please rebuild and rerun the 150 % test case. The new build forces Vulkan log verbosity to verbose, so you’ll see `RenderPass.*` entries (plus swapchain/bitmap traces) straight away. Each snapshot should show 1800×750 attachments, layout, and usage data—share that output (plus what you see on-screen) so we can determine whether the renderpass or cached surfaces are dropping resolution.
