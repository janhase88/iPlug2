# Sandbox Validation Checklist

## 1. Static review for unintended side effects
- Verified that the centralized sandbox header leaves every toggle at `0` by default and cascades to the parent `IPLUG_SANDBOX_ALL` switch so legacy builds retain their process-wide statics when no overrides are provided.
- Confirmed the hierarchy guard macros (`IPLUG_SANDBOX_ENSURE_CHILD`) only assert when a child override diverges from its parent, blocking partial configurations that would otherwise desynchronize the module graph.
- Checked the Windows entry points and helper headers to ensure the new storage keywords (`IPLUG_SANDBOX_HINSTANCE_STORAGE`/`_EXTERN`) expand to the original global declarations unless sandbox toggles request isolation.
- Inspected the `IGraphicsWin` cache accessors to verify they return per-instance storage only when the sandbox macros are enabled while continuing to use the shared statics by default.
- Reviewed the Skia factory singletons and Vulkan logging sink to ensure their thread-local fallbacks are gated behind sandbox flags, leaving non-sandbox builds unchanged.

## 2. Follow-ups
- Document sandbox configuration workflows for downstream developers, covering MSBuild properties, override examples, and module-level expectations.
- Coordinate with Windows maintainers for manual verification runs because the container lacks the Windows toolchain needed for VST3/Vulkan builds.
