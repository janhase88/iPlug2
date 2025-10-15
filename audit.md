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
- Instrumentation of the coordinator confirmed that we always request the graphics/present queue family selected during device enumeration; the reported `vkGetDeviceQueue` violation arises from the validation layer not seeing the cached queue create info when the snapshot is reused, so we must ensure we do not mutate the recorded family index between clients.
- Enumerating the device extensions showed that modern NVIDIA drivers expose `VK_KHR_synchronization2`; Skia's Vulkan backend will schedule layout transitions such as `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL` whenever the runtime advertises support, which triggers validation errors unless the application enables the extension and associated feature struct during device creation.
- Our snapshot only tracked the mandatory swapchain extension, meaning the platform layer could neither enable optional extensions nor hand the actual enablement list to Skia. As a result Skia operated with stale capability data and still attempted synchronization2-only layouts while the driver considered the feature disabled.
- The coordinator discarded the `VkPhysicalDeviceSynchronization2Features` state after `vkCreateDevice`, so the platform layer could not surface whether synchronization2 had been toggled on when bootstrapping Skia.

## Proposed Remediation
1. Persist the list of enabled device extensions (including optional ones such as `VK_KHR_synchronization2`) inside the shared Vulkan snapshot so every client sees the canonical capability set.
2. Enable `VkPhysicalDeviceSynchronization2Features` at device creation whenever the driver exposes the extension, and surface that decision through the snapshot.
3. Feed the exact extension list and synchronization2 flag into the Windows platform layer so the `skgpu::VulkanExtensions` helper mirrors the runtime configuration that was actually negotiated with the driver.
4. Reset the cached capability state during teardown to prevent stale synchronization2 flags or extension arrays from leaking into the next device owner.


## Implementation Notes
- `WinVulkanDeviceCoordinator` now derives the canonical extension enablement list, toggles `VkPhysicalDeviceSynchronization2Features` when the driver offers the capability, and preserves both the list and feature struct in the shared snapshot.
- `IGraphicsWin` copies the enabled extension array and synchronization2 flag into its platform state, builds `skgpu::VulkanExtensions` with those exact names, and exports the information through the `VulkanContext` handed to `IGraphicsSkia`.
- `VulkanContext` includes the enabled-extension array and synchronization2 metadata so the Skia renderer can log and adapt to the runtime capabilities without guessing.
- Vulkan teardown on Windows now clears the cached extension list and synchronization2 feature struct to avoid leaking stale capability data into the next context.

