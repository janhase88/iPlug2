# Windows Sandbox Switch Audit (Skia/Vulkan Path)

## Environment
- Build/test compatibility: **No** — Windows-targeted Skia/Vulkan plug-ins depend on platform toolchains and drivers that are unavailable in this Ubuntu container, so verification relies on static code review.【F:Documentation/WindowsSandbox.md†L83-L91】

## Core IPlug toggles
- `IPLUG_SANDBOX_ALL`

  `FULLY SANDBOXED: NO` — the configuration header sets the master flag to `1` before the `#ifndef` fallback, preventing downstream projects from disabling the switch via build defines.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L27-L32】
- `IPLUG_SANDBOX_CORE`

  `FULLY SANDBOXED: NO` — this macro simply aliases `IPLUG_SANDBOX_ALL` and only participates in hierarchy assertions; no implementation files branch on it directly.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L34-L38】【F:IPlug/Sandbox/IPlugSandboxConfig.h†L167-L170】
- `IPLUG_SANDBOX_DLL_ENTRY`

  `FULLY SANDBOXED: NO` — the DLL entry helpers ignore the toggle, so `DllMain` always runs and mutates global state regardless of the sandbox configuration.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L39-L42】【F:IPlug/IPlug_include_in_plug_src.h†L27-L38】
- `IPLUG_SANDBOX_HINSTANCE`

  `FULLY SANDBOXED: YES` — when enabled the header promotes `gHINSTANCE` to `thread_local` storage and routes every entry point through `IPLUG_SANDBOX_SET_HINSTANCE`, isolating per-thread handles while keeping a shared initializer for new threads.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L205-L225】【F:IPlug/IPlug_include_in_plug_src.h†L27-L38】
- `IPLUG_SANDBOX_HOST_CACHE`

  `FULLY SANDBOXED: YES` — the DPI helper pointer becomes `thread_local`, preventing cached host metrics from leaking across plug-in instances or threads.【F:IPlug/IPlug_include_in_plug_src.h†L40-L69】
- `IPLUG_SANDBOX_VST3`

  `FULLY SANDBOXED: NO` — the flag mirrors `IPLUG_SANDBOX_ALL`, and the shared VST3 entry points never branch on it, so factories and controllers still rely on process-wide statics.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L54-L57】【F:IPlug/IPlug_include_in_plug_src.h†L101-L190】
- `IPLUG_SANDBOX_VST3_FACTORY`

  `FULLY SANDBOXED: NO` — no factory registration code consults this toggle, leaving registrar singletons shared for every instance.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L59-L63】【F:IPlug/IPlug_include_in_plug_src.h†L143-L190】
- `IPLUG_SANDBOX_VST3_PROCESSOR`

  `FULLY SANDBOXED: NO` — processor creation paths do not test the macro, so audio-processing globals remain process-wide.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L64-L68】【F:IPlug/IPlug_include_in_plug_src.h†L138-L180】
- `IPLUG_SANDBOX_VST3_CONTROLLER`

  `FULLY SANDBOXED: NO` — controller/UI setup ignores the switch, leaving editor state shared across plug-ins.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L69-L72】【F:IPlug/IPlug_include_in_plug_src.h†L158-L189】

## Windows graphics toggles
- `IGRAPHICS_SANDBOX_WIN`

  `FULLY SANDBOXED: NO` — the macro simply aliases the global master flag, and the Windows renderer only branches on the child toggles (`WIN_CLASS`, `WIN_TIMERS`, `WIN_FONTS`), so this switch never drives isolation logic on its own.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L74-L92】【F:IGraphics/Platforms/IGraphicsWin.cpp†L49-L114】
- `IGRAPHICS_SANDBOX_WIN_CLASS`

  `FULLY SANDBOXED: YES` — each `IGraphicsWin` instance generates a unique window-class name, tracks its own registration count, and unregisters during teardown so HWNDs cannot collide between plug-ins.【F:IGraphics/Platforms/IGraphicsWin.cpp†L49-L132】【F:IGraphics/Platforms/IGraphicsWin.cpp†L1622-L1701】【F:IGraphics/Platforms/IGraphicsWin.cpp†L1884-L1894】【F:IGraphics/Platforms/IGraphicsWin.h†L241-L257】
- `IGRAPHICS_SANDBOX_WIN_TIMERS`

  `FULLY SANDBOXED: NO` — the toggle only changes the `sFPS` accumulator from a global to `thread_local`; timer creation and dispatch still operate on shared Win32 timers.【F:IGraphics/Platforms/IGraphicsWin.cpp†L66-L70】【F:IGraphics/Platforms/IGraphicsWin.cpp†L296-L336】
- `IGRAPHICS_SANDBOX_WIN_FONTS`

  `FULLY SANDBOXED: YES` — font caches move from shared statics to per-instance `StaticStorage` objects that are retained in the constructor and released in the destructor, preventing cross-instance font leaks.【F:IGraphics/Platforms/IGraphicsWin.cpp†L93-L132】【F:IGraphics/Platforms/IGraphicsWin.cpp†L851-L875】【F:IGraphics/Platforms/IGraphicsWin.h†L251-L257】

## Vulkan graphics toggles
- `IGRAPHICS_SANDBOX_VULKAN`

  `FULLY SANDBOXED: NO` — the macro aliases the master flag and the Vulkan context setup never checks it, leaving device creation and teardown identical regardless of the setting.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L94-L112】【F:IGraphics/Platforms/IGraphicsWin.cpp†L1147-L1281】
- `IGRAPHICS_SANDBOX_VK_DEVICE`

  `FULLY SANDBOXED: NO` — the macro is defined as an alias of `IGRAPHICS_SANDBOX_VULKAN`, and the Windows Vulkan context code always initialises the device coordinator without consulting this flag, so enabling it adds no extra isolation guarantees.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L99-L112】【F:IGraphics/Platforms/IGraphicsWin.cpp†L1147-L1281】
- `IGRAPHICS_SANDBOX_VK_SWAPCHAIN`

  `FULLY SANDBOXED: NO` — the macro simply mirrors `IGRAPHICS_SANDBOX_VULKAN`, and swapchain management in `IGraphicsWin` unconditionally maintains the same member caches, so the toggle cannot influence isolation.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L104-L112】【F:IGraphics/Platforms/IGraphicsWin.h†L177-L195】
- `IGRAPHICS_SANDBOX_VK_CONTEXT`

  `FULLY SANDBOXED: NO` — Vulkan context bootstrap caches live on `IGraphicsSkia` regardless of this flag, and the macro only aliases the umbrella toggle, leaving the subsystem unchanged when it is enabled.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L109-L117】【F:IGraphics/Drawing/IGraphicsSkia.h†L259-L288】

## Shared drawing toggles
- `IGRAPHICS_SANDBOX_DRAW`

  `FULLY SANDBOXED: NO` — the draw umbrella flag aliases the master toggle, while the concrete caches are still controlled by the specific child switches such as `IMAGE_CACHE`, so setting this macro alone has no effect.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L114-L135】【F:IGraphics/IGraphics.cpp†L43-L97】
- `IGRAPHICS_SANDBOX_TEXTURE_CACHE`

  `FULLY SANDBOXED: NO` — the macro is defined but unused; the shared caches inside `IGraphics` only branch on `IGRAPHICS_SANDBOX_IMAGE_CACHE`, leaving textures unaffected by this switch.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L119-L123】【F:IGraphics/IGraphics.cpp†L43-L79】
- `IGRAPHICS_SANDBOX_IMAGE_CACHE`

  `FULLY SANDBOXED: YES` — bitmap and SVG caches move from global statics to per-instance members guarded by `StaticStorage`, eliminating cross-plug-in decoded image leaks.【F:IGraphics/IGraphics.cpp†L43-L97】【F:IGraphics/IGraphics.h†L1873-L1876】
- `IGRAPHICS_SANDBOX_SKIA_FONT_CACHE`

  `FULLY SANDBOXED: YES` — the Skia font cache switches from a shared static to the renderer's `mFontCache`, with retain/release guarded by the toggle.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L677-L907】【F:IGraphics/Drawing/IGraphicsSkia.h†L226-L228】
- `IGRAPHICS_SANDBOX_FONT_FACTORY`

  `FULLY SANDBOXED: YES` — Skia font-manager singletons become thread-local and each renderer keeps its own `mFontMgr` instance when the flag is set, preventing global factory reuse.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L832-L918】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L2294-L2343】
- `IGRAPHICS_SANDBOX_UNICODE_HELPER`

  `FULLY SANDBOXED: YES` — enabling the switch gives every renderer its own Unicode helper guarded by thread-local creation flags instead of the shared singleton.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L856-L939】【F:IGraphics/Drawing/IGraphicsSkia.h†L241-L249】

## Logging toggles
- `IGRAPHICS_SANDBOX_LOGGING`

  `FULLY SANDBOXED: NO` — the macro mirrors the master flag and the Vulkan logging helpers only check `IGRAPHICS_SANDBOX_VK_LOGGER`, so this switch never drives behaviour on its own.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L144-L157】【F:IGraphics/Platforms/VulkanLogging.h†L75-L94】
- `IGRAPHICS_SANDBOX_VK_LOGGER`

  `FULLY SANDBOXED: NO` — the log sink becomes `thread_local`, which still forces plug-ins sharing the same render thread to multiplex a single sink instead of binding per editor instance.【F:IGraphics/Platforms/VulkanLogging.h†L75-L93】
- `IGRAPHICS_SANDBOX_VK_LOG_LEVEL`

  `FULLY SANDBOXED: NO` — the macro exists only in the sandbox config; the Vulkan logging utilities never read it, so log verbosity remains process-wide.【F:IPlug/Sandbox/IPlugSandboxConfig.h†L154-L157】【F:IGraphics/Platforms/VulkanLogging.h†L75-L94】

## Summary
Only the `HINSTANCE`, DPI cache, Win32 window class/font caches, Skia image/font factories, and Unicode helpers provide full per-instance isolation today. VST3 modules, Vulkan device/swapchain/context lifetimes, texture caches, and Vulkan logging controls still rely on process-wide or thread-shared state, so further implementation work is required before those toggles can be declared fully sandboxed.
