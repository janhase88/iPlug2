[x] Continued From Previous Snapshot: YES — Continuing the Windows VST3 Skia/Vulkan DPI investigation with host/physical scale separation.
-----------------
[x] File Overview:
- Num files Changed: 3
- Num files Created: 0
- Num lines Modified: <100 (build flag hook + status docs)

[x] List of Files changed/created:
- Changed:
  - common-win.props
  - AgentChat.md
  - Plan/Plan-Summary.md

-----------------
[x] Current plan:
- DPI Investigation — PREVIOUS STATUS: IMPLEMENTATION / CURRENT STATUS: IN PROGRESS
  - Step 1: Verify Windows virtualization ratio logging — PREVIOUS STATUS: UPDATED / CURRENT STATUS: COMPLETE (logs now emit host/effective/raw DPI; latest run shows Windows not virtualizing this HWND).
  - Step 2: Force Skia DPR to 1.5× and capture crispness result — PREVIOUS STATUS: READY / CURRENT STATUS: COMPLETE (multiple hard-coded/forced runs remained blurry; keeping override tools for future checks).
  - Step 3: Inspect Vulkan renderpass / cached surfaces — PREVIOUS STATUS: PENDING / CURRENT STATUS: IN PROGRESS (next commits will instrument attachments and cached surfaces to locate the resolution drop).

[x] Message to User:
`common-win.props` now exposes `IGRAPHICS_SKIA_FORCE_DEVICE_SCALE_MILLIS`, so you can flip the forced DPR directly in Project Properties without a shell. I’m moving on to Vulkan renderpass/cache instrumentation to find the blur since the 1.5× runs still look soft—next update will include that evidence.
