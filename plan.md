# Remediation Plan for Vulkan Validation Errors

1. **Codebase Reconnaissance**
   - Map out Windows Skia Vulkan initialization/teardown code paths (`IGraphicsWin`, `IGraphicsSkia`, `WinVulkanDeviceCoordinator`).
   - Identify resource creation sites (command buffers, descriptor pools, swapchain, Skia surfaces).

2. **Reproduce Error Conditions via Static Analysis**
   - Trace execution order for first frame submission and teardown.
   - Document which queue families and pipeline barriers are used.

3. **External Research**
   - Review Vulkan specification sections referenced by validation layers.
   - Consult Skia Vulkan backend integration guides/best practices.

4. **Root Cause Analysis**
   - Correlate observed validation errors with code paths.
   - Determine misconfigurations (queue family indices, image creation parameters, barrier usage, descriptor pool flags).

5. **Implement Fixes**
   - Refactor initialization to request correct queue families and configure Skia surfaces appropriately.
   - Ensure command buffer barriers use valid stage/access masks and layouts without requiring synchronization2.
   - Introduce a Vulkan procedure shim so Skia obtains compatibility wrappers for problematic entry points (`vkGetDeviceQueue`, `vkFlushMappedMemoryRanges`, `vkCmdPipelineBarrier`, `vkCreateImage`, `vkFreeDescriptorSets`).
     - Resolve wrappers via `VulkanBackendContext::fGetProc` so we can sanitize parameters before they reach the driver.
     - Adjust flush sizes to respect `nonCoherentAtomSize`, clamp invalid image depths, coerce layout transitions to spec-compliant values, and fall back to the configured graphics queue if Skia requests an unsupported queue family.
     - Replace descriptor-set frees with pool resets during teardown to avoid `FREE_DESCRIPTOR_SET_BIT` validation noise.
     - Maintain a device-level shim registry so wrapper dispatch remains stable even when Skia resolves procedures on background threads or after context recreation.
   - Propagate physical-device features/properties (including `VkPhysicalDeviceFeatures2` and memory limits) to Skia so flush alignment and layout decisions match the hardware capabilities.
     - Guard the propagation logic so it cooperates with older Skia SDKs that may lack the newer backend-context fields.

6. **Validation and Documentation**
   - Update audit log with findings and resolutions.
   - Summarize changes in PR description and final response.

