# Remediation Plan for Vulkan Validation Errors

1. **Codebase Reconnaissance** ✅
   - Map the Windows Vulkan bootstrap flow (`WinVulkanDeviceCoordinator`, `IGraphicsWin`, `IGraphicsSkia`).
   - Identify where extension lists, feature structs, and queue families are selected and cached.

2. **Specification & Vendor Research** ✅
   - Review the Vulkan 1.3 `VK_KHR_synchronization2` requirements and validation rules for `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL`.
   - Cross-check Skia's Vulkan backend expectations regarding extension enablement and backend-context metadata.

3. **Root Cause Confirmation** ✅
   - Inspect the shared device snapshot to verify which extensions/features are persisted across clients.
   - Compare the driver's advertised extension list with what we actually enable when creating the logical device.

4. **Refactor Capability Tracking** ✅
   - Record the enabled device extensions and synchronization2 feature struct in the shared snapshot.
   - Expose the synchronized capability state through `IGraphicsWin` and the `VulkanContext` passed to Skia.

5. **Runtime Integration Updates** ✅
   - Rebuild `skgpu::VulkanExtensions` with the exact runtime extension list.
   - Teach `IGraphicsSkia` to cache the synchronization2 flag for future command-buffer policy adjustments.

6. **Documentation & Follow-up** ✅
   - Update the audit log with the new findings and mitigations.
   - Summarize the remediation in the final report and PR body.

