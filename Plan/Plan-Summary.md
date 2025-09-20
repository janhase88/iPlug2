[x] Continued From Previous Snapshot: YES — Continuing the Windows sandbox fallback investigation for KickDrum.
-----------------
[x] File Overview:
- Num files Changed: 3
- Num files Created: 0
- Num lines Modified: 186

[x] List of Files changed/created:
- IGraphics/Platforms/IGraphicsWin.cpp
- Plan/Current-Plan.xml
- Plan/Plan-Summary.md

-----------------
[x] Current plan:
- Confirm linker failures stem from stripped fallback exports — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
- Provide MSVC alternatenames for the fallback bridge wrappers — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
- FINAL CHECK — PREVIOUS STATUS: OPEN / CURRENT STATUS: OPEN

[x] Message to User:
Replaced the __FUNCDNAME__ helper shims with C-linkage bridge wrappers and alternatename pragmas so missing WDL helpers resolve to the legacy fallbacks without requiring the helper translation units.
