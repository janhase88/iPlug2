# Renewed Remediation Plan for Windows Vulkan Validation Failures

1. **Re-Audit the Failure Stream** ✅
   - Re-read the latest startup/shutdown traces to catalog every validation ID that still fires.
   - Classify which reports stem from our code versus third-party overlays (e.g., GTIII-OSD, OBS).

2. **Deep-Dive Research** ✅
   - Cross-reference Vulkan 1.3 core promotion rules for `VK_KHR_synchronization2` and queue-family creation requirements using the Khronos spec and Skia's Ganesh Vulkan guide.
   - Review NVIDIA developer forum threads on swapchain layout transitions to ensure our barrier policy aligns with driver expectations.

3. **Capability Snapshot Fixes** ✅
   - Extend the coordinator snapshot so we distinguish "feature supported" from "extension string present" and handle Vulkan 1.3 cores that lack the legacy extension name.
   - Query `vkGetPhysicalDeviceFeatures2` to confirm the synchronization2 feature bit before enabling it.

4. **Skia Backend Context Corrections** ✅
   - Propagate the physical-device API version to Skia so it advertises the correct `fMaxAPIVersion` and can select the matching path.
   - Ensure the synchronization2 feature struct we enabled is forwarded to the renderer when available.

5. **Synchronization2 Migration** ✅
   - Re-enable Synchronization2 when the device supports it, avoiding duplicate feature structs on Vulkan 1.3.
   - Load the `vkCmdPipelineBarrier2` / `vkQueueSubmit2` entry points when the feature is active and fall back gracefully on legacy paths.

6. **Command Buffer Refactor** ✅
   - Replace legacy `VkImageMemoryBarrier` submissions in BeginFrame/EndFrame/flush prep with dual-path helpers that emit either classic barriers or Synchronization2 dependency chains.
   - Track swapchain image layouts precisely (including `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR`) so validation sees consistent history.

7. **Verification Strategy** ☐
   - Outline manual validation steps for the user (layers enabled, expected absence of specific VUIDs) once the changes are integrated.

