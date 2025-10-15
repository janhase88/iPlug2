# Renewed Remediation Plan for Windows Vulkan Validation Failures

1. **Re-Audit the Failure Stream** ✅
   - Re-read the latest startup/shutdown traces to catalog every validation ID that still fires.
   - Classify which reports stem from our code versus third-party overlays (e.g., GTIII-OSD, OBS).

2. **Deep-Dive Research** ✅
   - Cross-reference Vulkan 1.3 core promotion rules for `VK_KHR_synchronization2` and queue-family creation requirements using the Khronos spec and Skia's Ganesh Vulkan guide.
   - Review NVIDIA developer forum threads on swapchain layout transitions to ensure our barrier policy aligns with driver expectations.

3. **Device Capability Tracking Fixes** ✅
   - Extend the coordinator snapshot so we distinguish "feature supported" from "extension string present" and handle Vulkan 1.3 cores that lack the legacy extension name.
   - Query `vkGetPhysicalDeviceFeatures2` to confirm the synchronization2 feature bit before enabling it.

4. **Skia Backend Context Corrections** ✅
   - Propagate the physical-device API version to Skia so it advertises the correct `fMaxAPIVersion` and can select the matching path.
   - Ensure the synchronization2 feature struct we enabled is forwarded to the renderer when available.

5. **Documentation & Traceability** ✅
   - Update the audit log with the refined analysis and mitigation steps.
   - Mirror the latest exchange and conclusions in `chatprotocol.md` for continuity.

6. **Verification Strategy** ☐
   - Outline manual validation steps for the user (layers enabled, expected absence of specific VUIDs) once the changes are integrated.
