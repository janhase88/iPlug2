# Windows VST3 Vulkan Sandbox Configuration Contract

## 1. Macro naming scheme and hierarchy propagation
- **Master switch:** `IPLUG_SANDBOX_ALL` toggles sandboxing for every supported module. It is the only macro meant to be set by downstream projects; the central header fans out to module-level toggles based on this flag.
- **Module families:** The header groups toggles by the same hierarchies catalogued in the sandbox audit. Each family exposes a parent macro that defaults to the master switch value and forwards to child macros for individual shared resources.

| Family | Parent macro (evaluated first) | Child toggles (inherit parent unless explicitly overridden) | Scope | Notes |
| --- | --- | --- | --- | --- |
| IPlug Windows globals | `IPLUG_SANDBOX_CORE` | `IPLUG_SANDBOX_DLL_ENTRY`, `IPLUG_SANDBOX_HINSTANCE`, `IPLUG_SANDBOX_HOST_CACHE` | `IPlug/IPlug_include_in_plug_src.h`, VST3 factory, preset/path helpers | Parent replaces process-wide globals with per-instance storage. Child toggles control individual helpers so legacy hosts can re-enable shared caches if required. |
| VST3 factory & module registration | `IPLUG_SANDBOX_VST3` | `IPLUG_SANDBOX_VST3_FACTORY`, `IPLUG_SANDBOX_VST3_PROCESSOR`, `IPLUG_SANDBOX_VST3_CONTROLLER` | `IPlug/VST3/IPlugVST3.cpp`, `IPlug/VST3/IPlugVST3_Processor.cpp`, `IPlug/VST3/IPlugVST3_Controller.cpp` | Ensures the factory, processor, and controller do not consult static host descriptors or shared singletons when sandboxed. |
| Windows platform windowing | `IGRAPHICS_SANDBOX_WIN` | `IGRAPHICS_SANDBOX_WIN_CLASS`, `IGRAPHICS_SANDBOX_WIN_TIMERS`, `IGRAPHICS_SANDBOX_WIN_FONTS` | `IGraphics/Platforms/IGraphicsWin.cpp`, `IGraphics/Platforms/IGraphicsWin.h` | Parent macro reinitializes registration counters and timers per instance; children specialize font caches and the FPS tracker. |
| Vulkan device orchestration | `IGRAPHICS_SANDBOX_VULKAN` | `IGRAPHICS_SANDBOX_VK_DEVICE`, `IGRAPHICS_SANDBOX_VK_SWAPCHAIN`, `IGRAPHICS_SANDBOX_VK_CONTEXT` | `IGraphics/Platforms/WinVulkanDeviceCoordinator.*`, `IGraphics/Platforms/IGraphicsWin.cpp` | Converts the singleton coordinator into per-instance state when enabled, while allowing reuse of swap chains if explicitly requested. |
| Skia drawing caches | `IGRAPHICS_SANDBOX_DRAW` | `IGRAPHICS_SANDBOX_TEXTURE_CACHE`, `IGRAPHICS_SANDBOX_FONT_FACTORY`, `IGRAPHICS_SANDBOX_UNICODE_HELPER` | `IGraphics/Drawing/IGraphicsSkia.cpp` | Default ties the Skia caches to the owning graphics object; child macros let integrators selectively share heavy-weight factories. |
| Vulkan structured logging | `IGRAPHICS_SANDBOX_LOGGING` | `IGRAPHICS_SANDBOX_VK_LOGGER`, `IGRAPHICS_SANDBOX_VK_LOG_LEVEL` | `IGraphics/Platforms/VulkanLogging.h` | Parent swaps the static sink with a sandbox accessor. Child toggles allow different log targets or level overrides per plug-in. |

### Propagation rules
1. The central header defines each parent macro as `#if !defined(...)` before giving it a default (`#define IGRAPHICS_SANDBOX_VULKAN IPLUG_SANDBOX_ALL`).
2. Child macros only test whether they were defined earlier; if not, they inherit their parent: `#if !defined(IGRAPHICS_SANDBOX_VK_DEVICE)
   #define IGRAPHICS_SANDBOX_VK_DEVICE IGRAPHICS_SANDBOX_VULKAN
   #endif`.
3. Hierarchies never short-circuit upward—overriding a child does **not** mutate the parent. This keeps evaluation order deterministic even when multiple headers include the sandbox config.
4. Headers that own shared resources gate their statics with the child macro. Aggregator headers (`IPlug_include_in_plug_src.h`, `IGraphicsWin.h`) depend only on the parent switches to avoid redundant include work.

### Include order requirements
- Place the sandbox header (`IPlug/Sandbox/IPlugSandboxConfig.h`) immediately after platform defines but before module headers that declare globals. For plug-in entry points this means including it at the top of `IPlug_include_in_plug_src.h` and propagating via `IPlugVST3_Defs.h`.
- Platform projects must define any overrides **before** including the sandbox header. Downstream code should set overrides via build system defines (`/D` or `<PreprocessorDefinitions>`) rather than editing library sources.

## 2. Configuration defaults and override workflow

### Default behaviour
- All parent macros default to `0` (sandbox disabled) by defining `IPLUG_SANDBOX_ALL 0` if the downstream project has not set it. This preserves the existing shared-state behaviour when the central header is included without changes.
- When `IPLUG_SANDBOX_ALL` evaluates to `1`, every parent macro inherits that value unless explicitly overridden prior to including the header.
- The header emits a `#pragma message` (wrapped in `#ifdef _MSC_VER`) only when sandboxing is partially enabled, helping Windows consumers audit their configuration without affecting non-MSVC builds.

### Enabling sandboxing
1. **Global isolation:** Projects define `/DIPLUG_SANDBOX_ALL=1` (MSVC) or add `-DIPLUG_SANDBOX_ALL=1` to their build system. No additional defines are required; the central header handles propagation.
2. **Selective isolation:** Projects may keep `IPLUG_SANDBOX_ALL=0` and enable a specific family, e.g. `/DIGRAPHICS_SANDBOX_VULKAN=1`. Because child macros inherit parent values, overriding a parent implicitly toggles the entire family unless further child overrides reset them to `0`.
3. **Opt-out for heavy resources:** When `IPLUG_SANDBOX_ALL=1`, integrators can re-enable expensive shared resources by defining the corresponding child macro to `0` (e.g. `/DIGRAPHICS_SANDBOX_TEXTURE_CACHE=0`). The header protects against conflicting redefinitions by performing `#if defined(...) && (IGRAPHICS_SANDBOX_TEXTURE_CACHE != 0 && IGRAPHICS_SANDBOX_TEXTURE_CACHE != 1)` guards and emitting diagnostic `#error`s.

### Build system touchpoints
- **`common-win.props`:** Introduce a user property (`IPlugSandboxMode`) that forwards to the compiler as `IPLUG_SANDBOX_ALL=$(IPlugSandboxMode)`. When unset, the property defaults to `0`, matching legacy behaviour. Additional MSBuild items map optional overrides (e.g. `IGraphicsSandboxVulkan`) to `/D` definitions for per-target customization.
- **CMake generators (if used downstream):** Document a cache variable `IPLUG_SANDBOX_ALL` that injects definitions via `target_compile_definitions()` so that the same header logic applies to non-MSVC Windows builds.
- **Examples and templates:** Each Windows/VST3 example includes a short snippet in its project readme showing how to toggle sandboxing either globally or per-family.

### Documentation and telemetry hooks
- Central header includes Doxygen comments describing each macro and its inheritance rules. Logging macros route through the existing Vulkan logging facility so enabling sandboxing does not introduce new logging sinks.
- Provide an appendix in the developer documentation that enumerates expected side effects when each macro is toggled, referencing the audit’s file anchors for quick cross-checks.

## 3. Next implementation steps
1. Add the header at `IPlug/Sandbox/IPlugSandboxConfig.h` with the macro scaffolding described above.
2. Update `IPlug_include_in_plug_src.h`, `IPlugVST3.cpp`, `IGraphicsWin.h/.cpp`, `WinVulkanDeviceCoordinator.*`, `IGraphicsSkia.cpp`, and `VulkanLogging.h` to replace static storage with sandbox-aware branches.
3. Extend `common-win.props` and any Windows build templates to forward `IPLUG_SANDBOX_ALL` (and optional overrides) to the compiler command line.
4. Produce developer documentation demonstrating default, fully isolated, and mixed-mode configurations to guide partner validation.
