# Windows Sandbox Design Notes

## Summary
This document proposes how to extend iPlug2's Windows sandbox so the "WDL Windows path" behaves like an isolated per-instance subsystem when the sandbox switch is enabled. It covers the sandbox context structure, API adjustments for WDL helpers, and toggle alignment with the existing `IPlugSandboxConfig.h` hierarchy.

## 1. Sandbox context structure and lifecycle
### 1.1 Context anatomy
Introduce a dedicated context object that encapsulates every piece of state currently stored in the static globals documented during the audit. A representative C++ sketch is below:

```cpp
struct WdlWindowsSandboxContext {
  SandboxInstanceId id;                       // Unique per plug-in instance
  HINSTANCE module_handle;                    // Mirrors gHINSTANCE ownership【F:IPlug/IPlug_include_in_plug_src.h†L27-L69】
  struct Utf8HookState {
    ATOM combo_atom;                          // Formerly s_combobox_atom【F:WDL/win32_utf8.c†L86-L142】
    std::wstring property_prefix;             // Namespacing for WDL_UTF8_OLDPROCPROP【F:WDL/win32_utf8.c†L102-L170】
    std::vector<HookedControl> controls;      // Tracks HWND + old WNDPROCs per hook【F:WDL/win32_utf8.c†L1422-L1445】
  } utf8_hooks;
  struct FileDialogState {
    bool use_legacy_dialogs;                  // Replacement for wdl_use_legacy_filebrowse【F:WDL/filebrowse.cpp†L161-L257】
    ComPtr<IFileDialog> dialog;               // Wraps Win7FileDialog cached COM pointers【F:WDL/win7filedialog.cpp†L7-L123】
    ProcAddressCache shell_create_item;       // Sandboxes SHCreateItemFromParsingName cache【F:WDL/win7filedialog.cpp†L91-L114】
  } file_dialogs;
  struct DpiCache {
    FARPROC set_thread_awareness;             // Mirrors _WDL_dpi_func pointer cache【F:WDL/win32_hidpi.h†L13-L38】
    char mm_setwindowpos_state;               // Replacement for static init flag in WDL_mmSetWindowPos【F:WDL/win32_hidpi.h†L24-L55】
  } dpi;
  struct MenuState {
    std::vector<HMENU> popup_handles;         // Replaces implicit registries in win32_helpers helpers【F:WDL/win32_helpers.h†L6-L24】
  } menus;
  struct VirtualWindowState {
    bool helper_class_registered;             // Mirrors static reg flag【F:WDL/wingui/virtwnd.cpp†L1751-L1791】
    WNDPROC dialog_host_proc;                 // Stores vwndDlgHost_oldProc replacement【F:WDL/wingui/virtwnd.cpp†L1751-L1787】
    bool suppress_cursor_warp;                // Encapsulates wdl_virtwnd_nosetcursorpos【F:WDL/wingui/virtwnd.cpp†L1791-L1791】
  } virtual_windows;
  struct MenuSelectionState {
    HMENU active_menu = nullptr;              // Bridges TrackPopupMenu use in IGraphicsWin【F:IGraphics/Platforms/IGraphicsWin.cpp†L1980-L2052】
  } selection;
};
```

Key design choices:
- **Explicit namespacing:** `property_prefix` lets the sandbox mint unique property keys (e.g. `WDLUTF8OldProc/<id>`) so host-owned controls cannot collide across instances while reusing WDL hooking logic.【F:WDL/win32_utf8.c†L102-L170】
- **Composable substructures:** Grouping related state (UTF-8 hooks, file dialogs, DPI cache, etc.) keeps the context extensible if additional WDL helpers gain sandbox awareness later.
- **No process-wide singletons:** Every field replaces a static variable called out during the audit, ensuring that enabling sandbox mode is sufficient to isolate Windows resources.

### 1.2 Lifecycle integration
1. **Creation:** Instantiate `WdlWindowsSandboxContext` during the same bootstrap that currently initialises `gHINSTANCE` and the DPI cache inside `IPlug_include_in_plug_src.h`. Store the pointer in thread-local storage gated by `IPLUG_SANDBOX_HOST_CACHE` so each plug-in instance on Windows obtains its own context.【F:IPlug/IPlug_include_in_plug_src.h†L27-L69】
2. **Attachment:** When an `IGraphicsWin` view opens, provide it with a reference to the sandbox context so menu creation, cursor hiding, and dialog helpers can route through the same state object instead of recreating standalone caches.【F:IGraphics/Platforms/IGraphicsWin.cpp†L930-L1002】【F:IGraphics/Platforms/IGraphicsWin.cpp†L1980-L2052】
3. **Use:** WDL helpers receive a context pointer whenever they would have read or mutated a static. They lazily populate per-instance caches (e.g. the COM dialog pointer) and tag any Win32 properties or atoms with the sandbox’s namespace.
4. **Destruction:** When the plug-in instance unloads, tear down the context by undoing remaining hooks (restoring stored WNDPROCs) and releasing COM pointers. Because the context owns the handles, cleanup becomes deterministic even if the host forgets to unhook controls.
5. **Fallback:** If sandbox mode is disabled, continue using the existing global statics so legacy hosts see identical behaviour without requiring additional allocations or pointer chasing.

This lifecycle mirrors today’s sandbox bootstrap semantics while centralising ownership of every Windows-path resource.

## 2. API adjustments for WDL Windows helpers
### 2.1 Context-aware function signatures
- **UTF-8 hooks:** Extend `WDL_UTF8_HookComboBox/ListBox` and related helpers to accept `WdlWindowsSandboxContext&`. The helpers derive their atom/property names from `context.utf8_hooks` and stash old WNDPROCs inside the context instead of Win32 properties when possible, falling back to `SetProp` only for host-owned HWNDs.【F:WDL/win32_utf8.c†L102-L170】【F:WDL/win32_utf8.c†L1422-L1445】
- **File dialogs:** Replace the `wdl_use_legacy_filebrowse` global with a getter/setter on `context.file_dialogs`. `Win7FileDialog` constructors accept the context so cached shell function pointers and COM instances live in per-instance storage, eliminating the process-wide toggle observed today.【F:WDL/filebrowse.cpp†L161-L257】【F:WDL/win7filedialog.cpp†L7-L123】
- **DPI helpers:** Thread the context through `_WDL_dpi_func` and `WDL_mmSetWindowPos` so cached function pointers and the `init` flag become fields on `context.dpi`, aligning with the existing sandbox approach for `__GetDpiForWindow`.【F:WDL/win32_hidpi.h†L13-L55】【F:IPlug/IPlug_include_in_plug_src.h†L40-L69】
- **Menu utilities:** Introduce wrappers such as `WDL_InsertSubMenu(context, HMENU, ...)` that push created handles into `context.menus.popup_handles`. This mirrors how `IGraphicsWin::CreateMenu` currently manages `HMENU` lifetimes by hand, allowing a future migration to the shared helpers without losing per-instance tracking.【F:WDL/win32_helpers.h†L6-L24】【F:IGraphics/Platforms/IGraphicsWin.cpp†L1980-L2052】
- **Virtual windows:** Require `WDL_VWnd_regHelperClass` and cursor helpers to consume `context.virtual_windows` so the registration guard and cursor toggle no longer rely on process statics, matching `IGraphicsWin`'s per-instance cursor policies.【F:WDL/wingui/virtwnd.cpp†L1751-L1791】【F:IGraphics/Platforms/IGraphicsWin.cpp†L930-L1002】

### 2.2 Migration plan for IGraphics
1. Wrap existing bespoke Win32 implementations in `IGraphicsWin` with thin adapters that delegate to the context-aware WDL helpers when sandbox mode is active. For example, `PromptForFile` checks the sandbox toggle and either calls the new `WDL_ChooseFileForSave(context, ...)` variant or retains the current inline OpenFileNameW logic.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2205-L2311】
2. Convert menu creation to request handles through the context wrapper, which in turn records the menu lifetimes. This prepares the ground for sharing menu utilities across plug-ins without leaking `HMENU` identifiers.【F:IGraphics/Platforms/IGraphicsWin.cpp†L1980-L2052】
3. Replace cursor hiding/locking with context-driven helpers so tablet-aware behaviour remains per-instance. The virtual window state can own the `wdl_virtwnd_nosetcursorpos` flag while exposing accessors used by `IGraphicsWin::HideMouseCursor` and `MoveMouseCursor`.【F:IGraphics/Platforms/IGraphicsWin.cpp†L930-L1002】【F:WDL/wingui/virtwnd.cpp†L1751-L1791】
4. Provide transitional shims that map legacy APIs to the context-aware versions, minimising signature churn for downstream plug-ins until they opt into the new headers.

## 3. Aligning toggles with `IPlugSandboxConfig.h`
1. **New feature flag:** Introduce `#define IGRAPHICS_SANDBOX_WDL_WINDOWS IGRAPHICS_SANDBOX_WIN` to gate the context plumbing. It inherits the existing Windows sandbox family so enabling `IPLUG_SANDBOX_ALL` or `IGRAPHICS_SANDBOX_WIN` automatically covers the WDL helpers while still allowing granular opt-out for debugging.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L15-L122】
2. **Compile-time storage macros:** Mirror the pattern used for `IPLUG_SANDBOX_HINSTANCE` by adding macros (`WDL_SANDBOX_CONTEXT_STORAGE`, `WDL_SANDBOX_CONTEXT_INIT`, etc.) that expand to thread-local storage when the toggle is active. Legacy builds expand to plain globals, keeping binary compatibility.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L200-L223】
3. **Runtime switch:** Expose an optional `IPlugSandboxConfig::EnableWdlWindowsSandbox(bool)` helper that flips the flag per instance before any Windows UI is created. This allows hosts to disable the isolation at runtime for troubleshooting while ensuring that once a context exists, its configuration remains immutable, answering the risk summary’s question about mid-session mutations.【F:Plan/Artifacts/Windows-Sandbox-Risk-Summary.md†L15-L24】
4. **Backward compatibility:** When the toggle is off, the new wrappers immediately forward to the legacy static implementations, preserving existing behaviour and guaranteeing that plug-ins compiled without sandbox support do not incur extra allocations or code size.
5. **Testing hooks:** Document manual verification steps for Windows hosts (e.g. ensuring two sandboxed plug-ins no longer share file dialog folders) and add assertions that fail fast if sandbox-aware wrappers are called without an active context, providing deterministic failures in non-sandbox builds.

With these changes, enabling the sandbox switch will isolate every Windows-path resource without surprising legacy users, while the modular context design keeps future WDL migrations incremental.
