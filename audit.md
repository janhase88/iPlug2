# Windows Vulkan Validation Re-Audit

## Updated Context
- Startup and shutdown still trigger validation errors centered on queue acquisition, image layout transitions, and descriptor pool teardown.
- OBS, ASUS GTIII, and other overlays hook the Vulkan runtime; their layers generate additional warnings (e.g., descriptor pool free without `FREE_DESCRIPTOR_SET_BIT`). We document them but focus on defects attributable to iPlug2.

## Validation Findings (2024-07-03)
1. **`vkGetDeviceQueue` VUID-vkGetDeviceQueue-queueFamilyIndex-00384**
   - Fired immediately after device creation when validation rejected the feature chain; follow-on queue queries then emitted the warning.
2. **`vkCreateDevice` VUID-VkDeviceCreateInfo-pNext-06532**
   - Triggered because we appended both `VkPhysicalDeviceSynchronization2Features` and `VkPhysicalDeviceVulkan13Features` when targeting Vulkan 1.3.
3. **Legacy Barrier Incompatibilities**
   - Skia emits `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL` transitions whenever Synchronization2 is reported available.
   - Our renderer still records legacy `vkCmdPipelineBarrier` commands, so enabling Synchronization2 (or reporting Vulkan 1.3 support) causes validation to flag unsupported layout/stage mask pairings.
4. **Third-Party Noise**
   - GTIII-OSD and OBS trigger the `vkCreateImage` depth, `vkFlushMappedMemoryRanges` size, and descriptor pool warnings. They are noted but outside iPlug2's control.

## Research Notes
- Khronos Vulkan 1.3 specification (§39.6.2/§39.6.3) requires either core 1.3 support or the `VK_KHR_synchronization2` extension before `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL` and `VK_PIPELINE_STAGE_NONE` become valid enumerants.
- `vkEnumerateInstanceVersion` allows applications to negotiate the loader's highest supported API level so promoted core features remain available even when the extension strings disappear.
- Skia's Ganesh Vulkan backend (docs/ganesh/vk.md) expects `VulkanBackendContext::fMaxAPIVersion` and the synchronization2 struct to reflect the runtime, enabling it to pick the appropriate command buffer implementation.

## Remediation Plan Execution
- [x] Distinguish between "extension string present" and "feature supported" inside `WinVulkanDeviceCoordinator`.
- [x] Query `vkGetPhysicalDeviceFeatures2` to confirm the synchronization2 feature bit before enabling it.
- [x] Clamp the extension enablement array so we only append `VK_KHR_synchronization2` when the driver exposes the name.
- [x] Export the physical device's `apiVersion` through `VulkanContext` and set Skia's `fMaxAPIVersion` accordingly.
- [x] Re-enable Synchronization2 with a Vulkan 1.3–compliant feature chain and migrate our command buffers to `vkCmdPipelineBarrier2`/`vkQueueSubmit2` when available.
- [x] Teach the Skia bridge to track swapchain image layouts that pass through `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR`, using dual-path barrier helpers so validation remains satisfied on both legacy and sync2 runtimes.

## Verification Checklist (post-fix)
- Launch with validation layers enabled.
- Confirm absence of VUID 00384, 00957, 02254, 01390, 02820, 03937, 07794, and 07889 during startup/shutdown.
- Validate that Skia logs the Synchronization2 enablement state and negotiated API version consistently with the device snapshot.
