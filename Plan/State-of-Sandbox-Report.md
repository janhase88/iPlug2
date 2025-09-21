# State of Sandbox Report

## Completed toggles (Windows Skia/Vulkan path)
- `IGRAPHICS_SANDBOX_VK_CONTEXT` — SkiaVulkanContextState now scopes the Vulkan command pool, swapchain images, synchronization primitives, and logger context pointer per renderer, while helper accessors bind the owning logger whenever sandbox logging is enabled so legacy builds keep the shared singleton only when the toggle is disabled.【F:IGraphics/Drawing/IGraphicsSkia.h†L261-L339】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L211-L326】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1337-L1418】
- `IGRAPHICS_SANDBOX_DRAW` — The draw umbrella promotes the `IGraphics` bitmap and SVG caches to renderer-owned storage whenever it or its child toggles are active, leaving the static caches only for legacy, non-sandboxed builds.【F:IGraphics/IGraphics.cpp†L43-L103】【F:IGraphics/IGraphics.h†L1873-L1876】

## Documentation updates
- Expanded the Windows sandbox guide to describe the new Vulkan context isolation and draw umbrella semantics so Windows hosts know what to expect when enabling these toggles.【F:Documentation/WindowsSandbox.md†L79-L83】

## Outstanding work
- Await Windows validation runs to exercise the per-instance Vulkan state and draw caches under real hosts once tooling is available.
