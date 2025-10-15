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
- `WinVulkanDeviceCoordinator` only exposed a minimal snapshot; Skia never saw which device extensions/features were actually enabled.
- `IGraphicsSkia::OnViewInitialized` populated `VulkanBackendContext` without forwarding extension metadata, device features, or physical-device limits, so Skia assumed default capabilities (non-coherent atom size of 1, availability of synchronization2 layouts, unrestricted queue reuse, etc.).
- Because Skia lacked the real hardware limits, it issued flushes that violated the non-coherent atom-size requirement and chose image layouts (`VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL`) gated on synchronization2 support.
- The descriptor pool warning is consistent with Skia freeing descriptor sets on a pool that was created internally without the `FREE_DESCRIPTOR_SET_BIT`; once Skia sees the true extension set it can avoid freeing pools that do not advertise the capability.

## Proposed Remediation
1. Extend the Windows Vulkan coordinator to capture and expose enabled features, properties, memory limits, and extension strings.
2. Materialize a shared `skgpu::VulkanExtensions` instance during context creation and hand it (plus the queried device metadata) to `IGraphicsSkia` via `VulkanContext`.
3. Populate `skgpu::VulkanBackendContext` with the forwarded feature/property pointers when constructing the Skia direct context, using compile-time guards so older Skia SDKs still build.
4. Reset the cached metadata during teardown to prevent dangling pointers once the Vulkan device is destroyed.

## Implementation Notes
- `WinVulkanDeviceCoordinator` now stores the enabled device features in its snapshot so downstream consumers can advertise accurate capabilities to Skia.
- `IGraphicsWin` captures physical-device properties, memory limits, and extension availability during context creation, initialises a persistent `skgpu::VulkanExtensions`, and threads these pointers through `VulkanContext`.
- `IGraphicsSkia` forwards the extension pointer, device features, optional `VkPhysicalDeviceFeatures2`, properties, and memory limits into `skgpu::VulkanBackendContext`. Compile-time helpers ensure we only write to fields that exist in the linked Skia SDK.
- The backend now uses the standard `vkGetInstanceProcAddr` / `vkGetDeviceProcAddr` resolver directly; with the additional metadata in place Skia can respect the hardware’s non-coherent atom size, queue family selection, and layout restrictions without an interception layer.
- Vulkan teardown clears Skia GPU resources, resets swapchain caches, and nulls the forwarded metadata pointers to avoid dangling references.
