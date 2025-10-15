# Remediation Plan for Vulkan Validation Errors

1. **Codebase Reconnaissance**
   - Review the Windows Vulkan coordinator (`WinVulkanDeviceCoordinator`), platform glue (`IGraphicsWin`), and Skia backend (`IGraphicsSkia`) to understand initialization, swapchain management, and teardown flows.
   - Catalogue where the swapchain images, command buffers, and synchronization primitives are created and recycled.

2. **Static Trace of Startup/Shutdown**
   - Walk through the first-frame acquisition and presentation code paths to map the queue family, layout transitions, and synchronization barriers that fire before the UI appears.
   - Inspect teardown to confirm which resources Skia expects us to release versus which the platform owns.

3. **Specification & Integration Research**
   - Revisit Vulkan spec sections cited by the validation layers (device queues, memory flush alignment, image layout transitions, descriptor pool semantics).
   - Consult Skia’s Vulkan backend guidelines to understand which pieces of device metadata it consumes from `VulkanBackendContext`.

4. **Root Cause Isolation**
   - Correlate each validation error with the responsible code path.
   - Verify whether Skia is missing device capability data (extensions, features, physical-device limits) that would otherwise keep it from issuing spec-violating calls during the bootstrap frames.

5. **Implement Targeted Fixes**
   - Extend the Windows Vulkan coordinator snapshot so it captures enabled device features, queried physical-device properties, memory limits, and a lazily initialised `skgpu::VulkanExtensions` instance.
   - Teach `IGraphicsWin` to initialise and cache this metadata during context creation, wire it into the `VulkanContext` struct, and clear it safely during teardown to avoid dangling pointers.
   - Update `IGraphicsSkia` to forward the shared extension pointer plus the device feature/property structs into `skgpu::VulkanBackendContext`, using compile-time guards so older Skia SDKs continue to build.
   - Replace the proc-shim experiment with the direct `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr` lambda Skia expects, relying on the newly supplied metadata so Skia can respect non-coherent atom sizes, queue families, and supported layouts without extra interception.
   - Ensure Vulkan teardown releases Skia GPU resources, resets swapchain caches, and nulls the forwarded metadata pointers.

6. **Validation & Documentation**
   - Re-run the static audit after the fixes to confirm the initialization and teardown paths now supply complete capability data to Skia.
   - Update the audit log and conversation protocol with the refined findings and solutions.
