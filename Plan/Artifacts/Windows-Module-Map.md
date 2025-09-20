# WDL Windows Module → iPlug Integration Map

## Summary
This inventory links WDL's Windows-specific helpers to the iPlug/IGraphics code that currently consumes or replicates their behaviour. It highlights shared resources that must be isolated once the sandbox switch covers the "WDL Windows path" and records whether the existing integration is direct (compiled in today) or mirrored by hand-written Win32 logic.

| WDL module | Shared resource(s) | iPlug/IGraphics integration touchpoints | Sandboxing observations |
| --- | --- | --- | --- |
| `win32_utf8.c` | Static combo-box hook atom and UTF-8 overrides for Win32 text APIs (`s_combobox_atom`, `WDL_UTF8_OLDPROCPROP`).【F:WDL/win32_utf8.c†L78-L158】 | UTF-16/UTF-8 conversions inside `IGraphicsWin::PromptForFile` and related helpers currently reimplement the conversions directly instead of deferring to WDL wrappers.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2207-L2299】 | Any future adoption of the WDL override layer must route the hook state (combo atoms, subclass procs) through the sandbox context so per-instance dialogs do not share global handles. |
| `filebrowse.cpp` + `win7filedialog.cpp` | Global `wdl_use_legacy_filebrowse` switch; cached COM dialog objects in `Win7FileDialog` helper.【F:WDL/filebrowse.cpp†L161-L260】【F:WDL/win7filedialog.cpp†L1-L120】 | `IGraphicsWin::PromptForFile/PromptForDirectory` call native Common Dialog APIs directly, so adopting WDL's helpers would centralise file dialog state and respect the same sandbox switch that guards other Windows resources.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2207-L2333】 | If we swap to the WDL helpers, their globals (legacy toggle, COM handler storage) need per-instance storage; otherwise multiple plug-ins would share dialog preferences across the process. |
| `win32_helpers.h` | Inline helpers for inserting submenus and menu strings backed by global Win32 HMENU handles.【F:WDL/win32_helpers.h†L1-L24】 | `IGraphicsWin::CreateMenu` builds menus manually via `AppendMenuW`, `TrackPopupMenu`, and HMENU offsets.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2008-L2094】 | Reusing WDL helpers would consolidate menu creation, but we must ensure submenu registries (HMENUs) ride on sandbox context objects rather than static HMENU globals. |
| `wingui/virtwnd.cpp` (+ headers) | Global `wdl_virtwnd_nosetcursorpos` flag to control cursor warping, plus a virtual window hierarchy that caches HWND/SetCursorPos state.【F:WDL/wingui/virtwnd.cpp†L1780-L1791】 | `IGraphicsWin::HideMouseCursor` / `MoveMouseCursor` manually locks the cursor, recreating the same behaviour without the global toggle.【F:IGraphics/Platforms/IGraphicsWin.cpp†L946-L992】 | Introducing `WDL_VWnd` on Windows would require moving the global cursor toggle (and any static child lists) into per-instance storage so tablet-aware cursor logic remains isolated. |
| `IPlug` sandbox bootstrap | Process-wide globals such as `gHINSTANCE` and the DPI cache loader (`__GetDpiForWindow`) already thread-localise when sandbox flags are enabled.【F:IPlug/IPlug_include_in_plug_src.h†L27-L69】 | These entry points are the natural place to thread a sandbox context pointer that can be forwarded to the WDL modules listed above once they are made context-aware. | Extending the existing sandbox macros will let us reuse their storage model (thread-local wrappers plus shared fallback) for any WDL state we hoist out of static globals. |

## Module notes
### `WDL/win32_utf8`
* Provides UTF-8 aware wrappers for common window/control APIs and keeps a process-level atom (`s_combobox_atom`) plus subclass window procedures to track combo-box edits.【F:WDL/win32_utf8.c†L78-L158】  
* `IGraphicsWin` and `IPlugUtilities` already convert between UTF-8 and UTF-16 manually whenever they show dialogs or copy text, so the wrappers are not currently linked, but adopting them would centralise encoding logic.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2207-L2299】  
* Sandboxing implication: move the combo-box atom and hooked window proc pointers into a sandbox-owned structure before enabling the hooks per instance.

### `WDL/filebrowse` and `WDL/win7filedialog`
* `filebrowse.cpp` exposes global helpers for file dialogs and keeps a process-wide legacy toggle (`wdl_use_legacy_filebrowse`).【F:WDL/filebrowse.cpp†L161-L260】  
* The Vista/Win7 COM dialog wrapper (`Win7FileDialog`) stores interface pointers internally and performs UTF-8 conversions via helper allocators.【F:WDL/win7filedialog.cpp†L1-L120】  
* Because `IGraphicsWin::PromptForFile()` manages Win32 common dialogs directly today, switching to these helpers would reduce duplication but requires per-instance storage for the legacy flag and dialog COM state.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2207-L2333】 

### `WDL/win32_helpers`
* Supplies inline functions to insert submenus, separators, and menu strings while tracking the created HMENU handles.【F:WDL/win32_helpers.h†L1-L24】  
* `IGraphicsWin::CreateMenu()` mirrors this logic inline, creating and destroying menus in-place and mapping return values back to `IPopupMenu` objects.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2008-L2094】  
* To sandbox menu helpers, we must ensure each plug-in instance owns its menu handle registry (e.g., a sandbox-managed HMENU cache) rather than relying on static scope.

### `WDL/wingui`
* Implements the virtual window framework used by SWELL on other platforms and exposes global knobs such as `wdl_virtwnd_nosetcursorpos` that gate cursor warping for pen/tablet support.【F:WDL/wingui/virtwnd.cpp†L1780-L1791】  
* iPlug's Windows backend currently manages cursor lock/hide directly (`HideMouseCursor`, `MoveMouseCursor`), so there is no direct dependency yet.【F:IGraphics/Platforms/IGraphicsWin.cpp†L946-L992】  
* Should we import `WDL_VWnd` on Windows, cursor toggles and child hierarchies need to migrate into a sandbox context so multiple editors do not share the same static flag.

### Existing sandbox bootstrap (`IPlug_include_in_plug_src.h`)
* `gHINSTANCE` and the DPI helper pointer already switch to thread-local storage when sandbox macros are enabled, demonstrating the intended isolation pattern.【F:IPlug/IPlug_include_in_plug_src.h†L27-L69】  
* These entry points give us a central location to instantiate and stash the sandbox context that would own the WDL module state enumerated above.
