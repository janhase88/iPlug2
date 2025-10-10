# AgentChat

- Just taught the Vulkan swapchain to ignore the host-provided `currentExtent` whenever the bypass is on, so it now allocates images at the measured physical pixel size instead of the virtualized one (which was causing the blur).
- Added explicit `SetHostContentScaleBypassed` telemetry plus a `bypassVirtualExtent` field in the Vulkan logs—after rebuilding you should see a toggle log followed by `RefreshPlatformScale`, `PlatformResize`, and swapchain messages that all report `bypass=true` and the physical dimensions.
