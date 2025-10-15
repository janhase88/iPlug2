# Vulkan Validation Error Audit

## Context
- User-reported validation errors occur during application startup and shutdown when using the Windows Skia Vulkan path.
- Errors cease once the UI is fully drawn, indicating initialization/teardown issues.

## Observed Validation Errors
- `vkGetDeviceQueue`: queueFamilyIndex not among those requested at device creation.
- `vkCreateImage`: extent.depth set to 4 for a 2D image (must be 1).
- `vkFlushMappedMemoryRanges`: size not multiple of nonCoherentAtomSize and not covering whole allocation.
- `vkCmdPipelineBarrier`: srcStageMask zero without synchronization2; dstAccessMask mismatches stage masks; layout transitions to READ_ONLY_OPTIMAL without enabling synchronization2; layout transitions conflicting with known layout.
- `vkFreeDescriptorSets`: descriptor pool lacks FREE_DESCRIPTOR_SET_BIT flag during teardown.

## Initial Hypotheses
- Swapchain/attachment images incorrectly configured during bootstrap before Skia caches settle.
- Command buffers submitted with incomplete synchronization configuration.
- Descriptor pool flags/usage mismatched during shutdown.

## Next Steps
- Inspect `IGraphicsSkia` and `IGraphicsWin` Vulkan initialization, swapchain management, and teardown code.
- Trace command buffer recording around first frame to understand pipeline barriers and image layouts.
- Audit descriptor pool creation and destruction paths.
- Research best practices for Skia Vulkan backend integration on Windows.


## Additional Findings
- `WinVulkanDeviceCoordinator` only exposes minimal device snapshot data; Skia backend lacks visibility into enabled features and extension support.
- `IGraphicsSkia::OnViewInitialized` builds `VulkanBackendContext` without populating `fVkExtensions` or device feature pointers, so Skia assumes default capabilities. Skia may therefore opt into synchronization2-only layouts (`VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL`) and emit incompatible barriers.
- `VulkanBackendContext` still lacked physical-device feature/property pointers, preventing Skia from respecting non-coherent atom sizes and layout restrictions during the initial frame flush.
- Descriptor pool creation inside Skia defaults to pools without `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`. The validation error indicates pools are later freed with `vkFreeDescriptorSets`, consistent with Skia expecting the flag when extensions advertise support.
- `vkFlushMappedMemoryRanges` alignment warnings align with Skia not receiving `VkPhysicalDeviceProperties::nonCoherentAtomSize` via backend context metadata.

## Proposed Remediation
1. Extend the Windows Vulkan coordinator to capture and expose enabled features, properties, and extension strings.
2. Materialize a `skgpu::VulkanExtensions` object during context creation and share it with `IGraphicsSkia` so Skia can gate synchronization features correctly.
3. Populate `VulkanBackendContext` with feature pointers, extension metadata, and physical-device limits when instantiating the Skia direct context.
4. Reset shared extension/feature state during teardown to avoid stale pointers.


## Implementation Notes
- `WinVulkanDeviceCoordinator` now records the enabled device features in its snapshot so the renderer can advertise accurate capability metadata to Skia.
- `IGraphicsWin` captures physical-device properties, memory limits, and extension availability during context creation. A persistent `skgpu::VulkanExtensions` instance is initialized with the same extension lists used during instance/device creation.
- `VulkanContext` transports the extension/feature/property pointers into `IGraphicsSkia`, which now forwards them into `skgpu::VulkanBackendContext` when constructing the Skia direct context.
- Added propagation of `VkPhysicalDeviceFeatures2`, `VkPhysicalDeviceProperties`, and `VkPhysicalDeviceMemoryProperties` so Skia aligns buffer flushes and layout decisions with the actual hardware limits reported by the coordinator.
- Skia teardown clears cached pointers to avoid dangling references after the Vulkan device is destroyed.
- Added a guarded forward declaration for `skgpu::VulkanExtensions` within `IGraphicsSkia.h` to ensure projects without the newer
  Skia public header still compile while sharing the pointer metadata.
- Introduced compile-time detection helpers so the backend context only writes physical-device property pointers when the linked
  Skia SDK exposes those fields, preserving compatibility with older toolchains while forwarding the data when available.
- Injected a Vulkan procedure shim through `VulkanBackendContext::fGetProc` so Skia receives sanitized wrappers for problematic API calls. The shim clamps invalid image creation parameters, aligns non-coherent memory flushes, upgrades pipeline barrier stage masks/layouts, redirects queue requests to the configured graphics queue family, and replaces unsupported descriptor-set frees with pool resets, preventing the startup/teardown validation noise.

