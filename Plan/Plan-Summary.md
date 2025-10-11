[x] Continued From Previous Snapshot: YES — Continuing the Windows VST3 Skia/Vulkan DPI investigation with host/physical scale separation.
-----------------
- Num files Changed: 4
- Num files Created: 0
- Num lines Modified: <200 (Vulkan renderpass instrumentation + status docs)

[x] List of Files changed/created:
- Changed:
  - AgentChat.md
  - IGraphics/Drawing/IGraphicsSkia.cpp
  - Plan/Plan-Summary.md
  - common-win.props

-----------------
[x] Current plan:
- DPI Investigation — PREVIOUS STATUS: IMPLEMENTATION / CURRENT STATUS: IN PROGRESS
  - Step 1: Verify Windows virtualization ratio logging — PREVIOUS STATUS: UPDATED / CURRENT STATUS: COMPLETE (logs now emit host/effective/raw DPI; latest run shows Windows not virtualizing this HWND).
- Step 2: Force Skia DPR to 1.5× and capture crispness result — PREVIOUS STATUS: COMPLETE / CURRENT STATUS: COMPLETE (forced DPR runs remain blurry, confirming the issue lies beyond Skia’s DPR setting).
- Step 3: Inspect Vulkan renderpass / cached surfaces — PREVIOUS STATUS: IN PROGRESS / CURRENT STATUS: IN PROGRESS (renderpass telemetry now streams automatically at verbose level, and Skia bitmap draws switch to nearest-neighbour sampling whenever the effective device scale already matches the backing scale so the logged 1800×750 surfaces stay visually crisp for comparison).

[x] Message to User:
Please rebuild and rerun the 150 % test case. Vulkan logging stays at verbose, and bitmap draws now skip linear downsampling when the surface is already at the physical pixel scale, so you should see `RenderPass.*` entries alongside a sharper UI. Share the output and what you see on-screen so we can confirm the renderpass retains the full 1800×750 detail.
