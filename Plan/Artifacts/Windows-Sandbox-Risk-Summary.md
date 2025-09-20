# Windows Sandbox Risk Summary

## High-risk integration areas
- **UTF-8 control hooks (`WDL/win32_utf8.c`)** — The shared combo-box atom, property keys, and fallback buffers are reused across every hooked control, so a single instance can leak subclass procedures and cached text into another unless the sandbox supplies per-instance hook state.【F:WDL/win32_utf8.c†L86-L118】【F:WDL/win32_utf8.c†L1547-L1729】
- **File dialogs (`WDL/filebrowse.cpp`, `WDL/win7filedialog.cpp`)** — The global legacy toggle and cached `SHCreateItemFromParsingName` pointer change behaviour for every plug-in in the host process, requiring sandbox-owned storage before WDL dialogs can be enabled safely.【F:WDL/filebrowse.cpp†L161-L206】【F:WDL/win7filedialog.cpp†L89-L123】
- **DPI helpers (`WDL/win32_hidpi.h`)** — Both the DPI-awareness function pointer and the `init` flag are static, so experimentation with awareness modes in one plug-in impacts every other window unless moved into context-bound caches.【F:WDL/win32_hidpi.h†L12-L60】
- **Virtual window framework (`WDL/wingui/virtwnd.cpp`)** — The registered helper class, stored dialog procedure, and global cursor toggle are one-time initialisations that currently assume process-wide ownership, conflicting with per-plug-in cursor policies.【F:WDL/wingui/virtwnd.cpp†L1751-L1791】
- **Hand-written Win32 paths in IGraphics** — Existing menu creation, cursor hiding, and common dialogs bypass WDL wrappers, meaning the migration must either adopt sandbox-aware helpers or thread the sandbox context through today’s bespoke implementations.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2001-L2094】【F:IGraphics/Platforms/IGraphicsWin.cpp†L946-L992】【F:IGraphics/Platforms/IGraphicsWin.cpp†L2228-L2311】

## Cross-cutting constraints
- The sandbox configuration already relocates `gHINSTANCE`, `__GetDpiForWindow`, and related caches behind thread-local storage, providing a template for how new Windows-path state should be hoisted when the sandbox switch is active.【F:IPlug/IPlug_include_in_plug_src.h†L27-L69】
- Any context object must gracefully downgrade to legacy behaviour when the sandbox is disabled so hosts that expect shared globals retain backwards-compatible semantics.
- WDL helpers with Win9x fallbacks or optional hooks (e.g., `WDL_UTF8_HookListView`) need a way to fail closed when the sandbox context is missing to avoid double-hooking native host controls.

## Open questions to resolve during design
1. How should combo-box and list-view hooks manage property namespaces when multiple sandboxes coexist with host-supplied controls? (Potentially requires prefixing property keys with a sandbox identifier.)
2. What is the minimum viable sandbox context API that satisfies both iPlug-owned windows and host-owned child controls without forcing large signature churn?
3. Can file dialog toggles remain mutable at runtime, or should the sandbox freeze configuration per instance to prevent mid-session cross-talk?
4. Which regression checks can run in non-Windows environments, and what manual verification remains mandatory on Windows hosts?

## Recommended next steps
- Fold these risks into the design task by defining a sandbox context struct that encapsulates the per-instance data called out above.
- Plan migration guides for IGraphics sites that currently duplicate WDL behaviour so they can swap to context-aware helpers incrementally.
- Update implementation subtasks with checkpoints for Windows-only testing, acknowledging container limitations for automated validation.
