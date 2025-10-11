[x] Continued From Previous Snapshot: YES — Continuing the Windows VST3 Skia/Vulkan DPI investigation with consolidated instrumentation.
-----------------
[x] File Overview:
- Num files Changed: 3
- Num files Created: 0
- Num lines Modified: 50-150 (DPI helpers + status docs)

[x] List of Files changed/created:
- Changed:
  - IGraphics/Platforms/IGraphicsWin.cpp
  - AgentChat.md
  - Plan/Plan-Summary.md

-----------------
[x] Current plan:
- DPI Investigation — PREVIOUS STATUS: IMPLEMENTATION / CURRENT STATUS: IN PROGRESS
  - Step 1: Verify Windows virtualization ratio logging — PREVIOUS STATUS: OPEN / CURRENT STATUS: READY FOR RE-TEST (new probe now derives virtualization from physical/logical pixels; expect `virtualization≈1.5`).
  - Step 2: Force Skia DPR to 1.5× and capture crispness result — PREVIOUS STATUS: OPEN / CURRENT STATUS: OPEN (awaiting forced-DPI run with `forced=1` in `IGraphicsSkia[DPI]`).
  - Step 3: Inspect Vulkan renderpass / cached surfaces if Step 2 remains blurry — PREVIOUS STATUS: BLOCKED / CURRENT STATUS: BLOCKED (dependent on Step 2 outcome).

[x] Message to User:
Run one pass without overrides to confirm the new logs report `virtualization≈1.5` (raw DPI) and a second pass with `IGRAPHICS_SKIA_FORCE_DEVICE_SCALE=1.5` so Skia prints `forced=1` / render target 1800×750. With those two captures we’ll know whether to dive straight into the Vulkan renderpass work.
