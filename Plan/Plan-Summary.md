[x] Continued From Previous Snapshot: YES — Continuing the Windows VST3 Skia/Vulkan DPI investigation while separating host vs. physical window scaling.
-----------------
- Num files Changed: 6
- Num files Created: 0
- Num lines Modified: <200 (host/physical scale handshake adjustments, DPI logging, and status docs)

[x] List of Files changed/created:
- Changed:
  - AgentChat.md
  - IGraphics/IGraphics.h
  - IGraphics/IGraphics.cpp
  - IGraphics/Platforms/IGraphicsWin.h
  - IGraphics/Platforms/IGraphicsWin.cpp
  - Plan/Plan-Summary.md

-----------------
[x] Current plan:
- DPI Investigation — PREVIOUS STATUS: IMPLEMENTATION / CURRENT STATUS: IN PROGRESS
  - Step 1: Verify Windows virtualization ratio logging — PREVIOUS STATUS: UPDATED / CURRENT STATUS: COMPLETE (logs now emit host/effective/raw DPI; latest run shows Windows not virtualizing this HWND).
- Step 2: Force Skia DPR to 1.5× and capture crispness result — PREVIOUS STATUS: COMPLETE / CURRENT STATUS: COMPLETE (forced DPR runs remain blurry, confirming the issue lies beyond Skia’s DPR setting).
- Step 3: Inspect Vulkan renderpass / cached surfaces — PREVIOUS STATUS: IN PROGRESS / CURRENT STATUS: IN PROGRESS (renderpass telemetry still streams automatically, host window math now keeps logical HWND sizes while swapchains allocate at the physical DPI, and we are analysing the resulting logs to pinpoint where resolution is lost).

[x] Message to User:
Please rebuild and rerun the 150 % test case. The new build keeps the HWND hierarchy at the host size while the swapchain/logs show the physical 1800×750 targets, so the latest `RenderPass.*` telemetry will let us compare what Skia submits versus what the host displays. Share those logs and what you see on-screen so we can continue narrowing down the blur source in Stage 3.
