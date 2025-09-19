# Windows VST3 Vulkan Sandbox Audit

## Scope
- **Target platform:** Windows-only code paths exercised by VST3 plug-ins using the Vulkan renderer.
- **Out of scope:** WDL utility code and anything in `Dependencies/`, per project constraints.
- **Objective:** Catalog the entry points and shared resources that allow cross-talk between plug-in instances so that new compile-time sandbox macros can gate them from a single configuration header.

## 1. Windows-specific initialization flow
### 1.1 VST3 entry points and host discovery
- `IPlugVST3::initialize()` chains the processor/controller initializers, queries the host via `IPlugVST3GetHost`, and triggers parameter resets, making it the first hook where sandbox defines must ensure per-instance state isolation.【F:IPlug/VST3/IPlugVST3.cpp†L47-L107】
- `IPlugVST3Processor::initialize()` mirrors the processor-side setup and reuses the shared host discovery helper, so guarding this path prevents shared static caches from being populated across processor/editor pairs.【F:IPlug/VST3/IPlugVST3_Processor.cpp†L36-L93】
- `IPlug_include_in_plug_src.h` exports global Windows state (`gHINSTANCE`) and DPI helpers for every Windows target, which currently live in a TU-level global scope and are reused by all plug-ins loaded in the host process. These are prime candidates for sandbox macros that redirect globals to per-instance containers.【F:IPlug/IPlug_include_in_plug_src.h†L21-L64】

### 1.2 Vulkan bootstrap inside the Windows platform layer
- `IGraphicsWin::CreateVulkanContext()` instantiates shared Vulkan device resources once per process by delegating to `WinVulkanDeviceCoordinator`. Without sandboxing, multiple plug-ins share `mVkInstance`, swap chains, and synchronization primitives guarded only by static process-wide members.【F:IGraphics/Platforms/IGraphicsWin.cpp†L1082-L1191】
- `WinVulkanDeviceCoordinator::Initialize()` caches the Vulkan snapshot internally (`mSnapshot`) and short-circuits subsequent calls if `mInitialized` is already true, meaning later plug-ins silently reuse the first plug-in's device selection. A sandbox macro must allow bypassing the singleton behaviour so each plug-in can own its device state.【F:IGraphics/Platforms/WinVulkanDeviceCoordinator.h†L50-L144】

## 2. Shared utilities leveraged by multiple plug-ins
| Module | Shared resource | Impact | Isolation strategy hint |
| --- | --- | --- | --- |
| `IPlug_include_in_plug_src.h` | Global `gHINSTANCE` and cached `GetDpiForWindow` function pointer | All Windows plug-ins share the same module handle and DPI lookup, preventing per-instance overrides or unload handling. | Wrap the globals in `IPLUG_SANDBOX_MODULE_SCOPE` to redirect storage into a sandbox context struct. | 【F:IPlug/IPlug_include_in_plug_src.h†L25-L63】
| `IGraphicsWin.cpp` | Static registration counters, FPS tracker, and font caches (`sPlatformFontCache`, `sHFontCache`) | These process-wide statics cause window classes, timer cadence (`sFPS`), and font resources to be reused across plug-ins, leading to cross-talk. | Introduce macros like `IGRAPHICS_SANDBOX_WIN_REGISTRY` to swap statics for context-owned members when sandboxing is enabled. | 【F:IGraphics/Platforms/IGraphicsWin.cpp†L46-L118】
| `IGraphicsSkia.cpp` | Global texture map reference plus once-only font/unicode factories (`gTextureMap`, `SkFontMgrRefDefault`, `GetUnicode`) | Texture caches and font factories are effectively singletons, so resources leak across plug-ins and make teardown order significant. | Gate the globals behind `IGRAPHICS_SANDBOX_SHARED_DRAWING` so each plug-in can opt into isolated caches or shared behaviour. | 【F:IGraphics/Drawing/IGraphicsSkia.cpp†L136-L218】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L812-L856】
| `VulkanLogging.h` | Global log sink stored in a static function-local slot | Structured Vulkan telemetry is routed through a shared sink, so sandboxing must allow per-plug-in sinks or disable logging entirely. | Provide a macro such as `IGRAPHICS_SANDBOX_VK_LOGGER` that swaps the static slot with a sandbox accessor. | 【F:IGraphics/Platforms/VulkanLogging.h†L26-L88】

## 3. Configuration touchpoints for the sandbox header
- `common-win.props` supplies the canonical include path lists and preprocessor definitions for Windows builds, including `VST3_API`, `IGRAPHICS_VULKAN_LOG_VERBOSITY`, and the Vulkan SDK linkage. Injecting the sandbox header here ensures every Visual Studio target sees the master define set without editing individual `.vcxproj` files.【F:common-win.props†L5-L98】
- Plugin translation units include `IPlug/IPlug_include_in_plug_src.h` directly, making it the earliest safe include for inserting `#include "IPlugSandboxConfig.h"` (or similar) before API-specific entry point macros expand.【F:IPlug/IPlug_include_in_plug_src.h†L16-L138】
- `IGraphics/Platforms/IGraphicsWin.h` already conditionally pulls in Vulkan helper headers based on `IGRAPHICS_VULKAN`, so adding a sandbox header include or macro forward declarations here will cascade through the renderer and platform glue without scattering manual guards.【F:IGraphics/Platforms/IGraphicsWin.h†L26-L182】

## 4. Next steps
1. Design sandbox macro names that mirror the module hierarchy (e.g., `IPLUG_SANDBOX_VST3_ENTRY`, `IGRAPHICS_SANDBOX_VK_DEVICE`).
2. Prototype the central header under a new include directory (e.g., `IPlug/Sandbox/`) and wire it into `common-win.props` and `IPlug_include_in_plug_src.h`.
3. Update the identified modules to honour the sandbox macros, migrating static storage into per-instance containers when sandboxing is enabled.
4. Document partner validation expectations (manual Windows builds with Vulkan validation layers) alongside the new configuration options.

