# Windows Sandbox Validation Checklist

This checklist documents the Windows-only manual tests required to verify the sandboxed WDL helper path until automated coverage is available.

## Prerequisites
- Build the plug-in with `IPLUG_SANDBOX_ALL=1` (or `IGRAPHICS_SANDBOX_WDL_WINDOWS=1`) so the sandboxed helpers are active.
- Use a host capable of running multiple instances in the same process (REAPER, Ableton Live, etc.).
- Enable verbose logging in debug builds to capture hook attach/detach diagnostics.

## UTF-8 helper isolation
1. Open two plug-in instances side by side.
2. Trigger any UI path that creates combo boxes, list boxes, or tree views (preset browsers, parameter menus).
3. Confirm each instance registers its own UTF-8 hook without overwriting the other:
   - Verify the per-instance property prefix (`IPlugSandbox/<id>/WDLUTF8OldProc`) appears on child HWNDs.
   - Destroy and recreate controls repeatedly; ensure `WDL_UTF8_Hook*Ctx` detaches without leaving stale hooks.
4. Inspect debugger output for the absence of access violations when rapidly closing editors.

## File dialog context
1. Invoke `PromptForFile`/`PromptForDirectory` in both instances using different starting directories.
2. Switch between legacy and Vista-style dialogs via `WDL_filebrowse` when applicable, ensuring each instance remembers its last directory independently.
3. Close the dialogs and reopen them from both instances; the cached `SHCreateItemFromParsingName` pointer should remain valid per context.

## Virtual window helpers and cursor guards
1. Interact with virtual window sliders and tooltip hosts that rely on `virtwnd.cpp` helper classes.
2. Confirm cursor locking/unlocking does not leak between instances when entering/leaving parameter edits.
3. Toggle DPI awareness (moving the window between monitors) and check that the sandbox resets `SetThreadDpiAwarenessContext` per instance.

## Regression logging
Record the outcomes of the above scenarios in project QA notes. If an issue appears, capture the instance ID (from `WdlWindowsSandboxContext::instance_id`) and the relevant hook diagnostics for debugging.
