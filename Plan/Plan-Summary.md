[x] Continued From Previous Snapshot: NO — restarted from the clean DPI baseline and layered instrumentation on top.
-----------------
[x] File Overview:
- Num files Changed: 5
- Num files Created: 0
- Num lines Modified: see diff

[x] List of Files changed/created:
- AgentChat.md
- IGraphics/IGraphics.cpp
- IGraphics/IGraphics.h
- IGraphics/Drawing/IGraphicsNanoVG.cpp
- Plan/plan.md

-----------------
[x] Current plan:
- Phase 1 — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
- Phase 2 — PREVIOUS STATUS: IN PROGRESS / CURRENT STATUS: SUCCESS
- Phase 3 — PREVIOUS STATUS: OPEN / CURRENT STATUS: OPEN

[x] Message to User:
Layers and cached bitmaps now inherit the backing pixel scale while resource loading targets the rounded physical DPI, so Skia should allocate native-resolution surfaces without bloating the logical layout. Rebuild the Windows VST3 Skia/Vulkan target at 100 % and 150 %, then share the updated `DrawResize`/`CreateAPIBitmap` logs so we can confirm the controls stay within bounds and finally look crisp.
