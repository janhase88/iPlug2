[x] Continued From Previous Snapshot: YES — Wrapped the sandbox context setter declaration so C builds no longer see a bare extern "C" token.
-----------------
[x] File Overview:
- Num files Changed: 1
- Num files Created: 0
- Num lines Modified: 8
-----------------
[x] List of Files changed/created:
- IPlug/Sandbox/IPlugSandboxConfig.h
-----------------
[x] Current plan:
- Audit Windows path integration points and shared resources — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
- Design sandbox-aware Windows path abstraction — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
- Implement sandboxed Windows path and regression coverage — PREVIOUS STATUS: PROOF / CURRENT STATUS: PROOF (header made C-compatible; Windows validation still pending.)
- FINAL CHECK — PREVIOUS STATUS: OPEN / CURRENT STATUS: OPEN
-----------------
[x] Message to User:
Ensured `IPlugSandboxConfig.h` can be consumed from C by guarding the sandbox setter prototype with `__cplusplus`.
Next step remains manual Windows validation before promoting the implementation task beyond PROOF.
