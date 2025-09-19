# Windows Sandbox Configuration Guide

## Purpose
The optional sandbox configuration in iPlug2 allows Windows plug-ins to behave as if each instance is hosted in isolation. When enabled, the macros in `Sandbox/IPlugSandboxConfig.h` ensure that shared globals—such as the module handle, Win32 window classes, font caches, Skia factories, and Vulkan logging sinks—are no longer shared across instances. This prevents state leaks between plug-ins that would otherwise reuse these resources inside the same process.

By default, all sandbox toggles are disabled (`0`) to preserve legacy behaviour. Projects opt in by defining `IPLUG_SANDBOX_ALL` (or specific child toggles) **before** including any iPlug headers.

## Macro hierarchy reference
The centralized header ships with a parent/child hierarchy so related subsystems cannot be partially enabled. Each entry defaults to its parent unless explicitly overridden.

| Macro | Defaults To | Protects |
| --- | --- | --- |
| `IPLUG_SANDBOX_ALL` | `0` | Master switch; enables all descendants. |
| `IPLUG_SANDBOX_CORE` | `IPLUG_SANDBOX_ALL` | Core DLL entry points, global host caches, shared `HINSTANCE`. |
| `IPLUG_SANDBOX_DLL_ENTRY` | `IPLUG_SANDBOX_CORE` | Guard `DllMain` and related entry helpers. |
| `IPLUG_SANDBOX_HINSTANCE` | `IPLUG_SANDBOX_CORE` | Wraps `gHINSTANCE` in thread-local storage. |
| `IPLUG_SANDBOX_HOST_CACHE` | `IPLUG_SANDBOX_CORE` | Isolates DPI/host capability caches per instance. |
| `IPLUG_SANDBOX_VST3` | `IPLUG_SANDBOX_ALL` | All VST3 module state. |
| `IPLUG_SANDBOX_VST3_FACTORY` | `IPLUG_SANDBOX_VST3` | Factory registration data. |
| `IPLUG_SANDBOX_VST3_PROCESSOR` | `IPLUG_SANDBOX_VST3` | Audio processor statics. |
| `IPLUG_SANDBOX_VST3_CONTROLLER` | `IPLUG_SANDBOX_VST3` | Controller/UI statics. |
| `IGRAPHICS_SANDBOX_WIN` | `IPLUG_SANDBOX_ALL` | Windows graphics subsystem. |
| `IGRAPHICS_SANDBOX_WIN_CLASS` | `IGRAPHICS_SANDBOX_WIN` | Window class registration. |
| `IGRAPHICS_SANDBOX_WIN_TIMERS` | `IGRAPHICS_SANDBOX_WIN` | Frame timer counters. |
| `IGRAPHICS_SANDBOX_WIN_FONTS` | `IGRAPHICS_SANDBOX_WIN` | Platform font caches. |
| `IGRAPHICS_SANDBOX_VULKAN` | `IPLUG_SANDBOX_ALL` | Vulkan renderer integration. |
| `IGRAPHICS_SANDBOX_VK_DEVICE` | `IGRAPHICS_SANDBOX_VULKAN` | Device handles and queues. |
| `IGRAPHICS_SANDBOX_VK_SWAPCHAIN` | `IGRAPHICS_SANDBOX_VULKAN` | Swapchain + image state. |
| `IGRAPHICS_SANDBOX_VK_CONTEXT` | `IGRAPHICS_SANDBOX_VULKAN` | Context bootstrap caches. |
| `IGRAPHICS_SANDBOX_DRAW` | `IPLUG_SANDBOX_ALL` | Drawing factories shared by multiple backends. |
| `IGRAPHICS_SANDBOX_TEXTURE_CACHE` | `IGRAPHICS_SANDBOX_DRAW` | Shared texture maps. |
| `IGRAPHICS_SANDBOX_FONT_FACTORY` | `IGRAPHICS_SANDBOX_DRAW` | Skia font manager globals. |
| `IGRAPHICS_SANDBOX_UNICODE_HELPER` | `IGRAPHICS_SANDBOX_DRAW` | Skia Unicode helper singletons. |
| `IGRAPHICS_SANDBOX_LOGGING` | `IPLUG_SANDBOX_ALL` | Graphics logging facilities. |
| `IGRAPHICS_SANDBOX_VK_LOGGER` | `IGRAPHICS_SANDBOX_LOGGING` | Vulkan log sink routing. |
| `IGRAPHICS_SANDBOX_VK_LOG_LEVEL` | `IGRAPHICS_SANDBOX_LOGGING` | Per-instance log verbosity. |

> **Hierarchy enforcement:** Each child performs a `static_assert` against its parent, so a partial configuration (for example, enabling `IGRAPHICS_SANDBOX_VK_LOGGER` while disabling `IGRAPHICS_SANDBOX_LOGGING`) will fail at compile time with a clear diagnostic.

## Enabling the sandbox
1. Define the desired macros before including any iPlug headers. Most projects can simply add `IPLUG_SANDBOX_ALL=1` to their compiler definitions. The header automatically validates the values are boolean (`0` or `1`).
2. Include iPlug's umbrella headers (`IPlug_include_in_plug_src.h`, `IGraphics_include_in_plug_src.h`) as usual. They already include `Sandbox/IPlugSandboxConfig.h`, so no additional includes are required. The header itself lives under `IPlug/Sandbox/` if you need to reference it directly.
3. Rebuild the plug-in. On MSVC, a build will emit `#pragma message` notes describing whether a full or partial sandbox configuration is active so misconfigurations are easy to spot.

### MSBuild usage
The Windows property sheet now exposes two properties:
- `IPlugSandboxMode` toggles `IPLUG_SANDBOX_ALL`.
- `IPlugSandboxExtraDefs` appends additional preprocessor overrides.

Example property group inside your `.vcxproj`:
```xml
<PropertyGroup>
  <IPlugSandboxMode>1</IPlugSandboxMode>
  <IPlugSandboxExtraDefs>IGRAPHICS_SANDBOX_VK_LOGGER=0</IPlugSandboxExtraDefs>
</PropertyGroup>
```
This enables full sandboxing but disables Vulkan log isolation if you prefer to share a global log sink across instances.

### Manual or alternative build systems
Projects that invoke the compiler directly should add equivalent definitions:
```bash
cl /DIPLUG_SANDBOX_ALL=1 /DIGRAPHICS_SANDBOX_TEXTURE_CACHE=1 ...
```
For CMake-based builds, apply the macros to the relevant target:
```cmake
target_compile_definitions(MyPlugin PRIVATE
  IPLUG_SANDBOX_ALL=1
  IGRAPHICS_SANDBOX_VK_LOGGER=1)
```
Ensure the definitions precede any `target_sources` that include iPlug headers so translation units see the correct configuration.

## Behavioural changes when sandboxing is enabled
- The Windows module handle (`gHINSTANCE`) keeps a thread-local slot backed by a shared fallback whenever `IPLUG_SANDBOX_HINSTANCE` is enabled, so new threads inherit the process handle while isolating writes; the DPI cache remains thread-local under `IPLUG_SANDBOX_HOST_CACHE`.【F:IPlug/IPlug_include_in_plug_src.h†L13-L71】【F:IPlug/Sandbox/IPlugSandboxConfig.h†L190-L218】
- Window classes switch from a single static name to per-instance registrations when `IGRAPHICS_SANDBOX_WIN_CLASS` is active, preventing HWND collisions between plug-ins that share the same process.【F:IGraphics/Platforms/IGraphicsWin.cpp†L43-L66】【F:IGraphics/Platforms/IGraphicsWin.h†L229-L260】
- Font caches (`InstalledFont` and `HFontHolder`) migrate from static globals to per-instance `StaticStorage` containers when `IGRAPHICS_SANDBOX_WIN_FONTS` is set, eliminating shared typography state.【F:IGraphics/Platforms/IGraphicsWin.cpp†L78-L111】【F:IGraphics/Platforms/IGraphicsWin.h†L235-L260】
- Skia factories (font manager and Unicode helpers) now rely on thread-local singletons whenever the draw sandbox is enabled, isolating GPU-backed resources for each plug-in thread.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L9-L35】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L825-L858】
- Vulkan logging routes through a thread-local sink so log consumers can capture per-instance telemetry without cross-talk when the logging sandbox is on.【F:IGraphics/Platforms/VulkanLogging.h†L1-L70】

## Validation workflow
Because this environment lacks the Windows VST3 Vulkan toolchain, validation is limited to static analysis. When enabling the sandbox in a Windows environment:
1. Build plug-ins with Vulkan validation layers enabled and confirm each instance emits logs only through its thread-local sink.
2. Inspect multiple simultaneous plug-ins to ensure window class names and font caches remain isolated (no shared handles reported by debug tooling).
3. Run hosts that reuse plugin DLLs (e.g., REAPER, Cubase) and confirm DPI scaling behaves consistently when instances are opened on different monitors.

Any issues should be cross-referenced with the audit (`Plan/Artifacts/Sandbox-Audit.md`) and validation checklist (`Plan/Artifacts/Sandbox-Validation.md`) for targeted troubleshooting steps.

## Troubleshooting
- **Compile-time assertion failure:** Ensure every overridden child macro keeps its parent enabled. For example, enable `IGRAPHICS_SANDBOX_LOGGING` whenever `IGRAPHICS_SANDBOX_VK_LOGGER` is `1`.
- **Missing diagnostics:** MSVC emits configuration hints only when compiling with `_MSC_VER`. Other compilers require manual inspection of the `IPLUG_SANDBOX_*` values in project files.
- **Shared state persists:** Verify the relevant module toggle is enabled and that the translation unit includes `Sandbox/IPlugSandboxConfig.h` before any conditional compilation occurs.

## Next steps
Future work can extend the sandbox hierarchy to additional modules identified in the audit (e.g., preset path helpers, VST3 factories) and add automated validation once Windows CI coverage is available.
