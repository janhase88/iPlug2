# AgentChat

- Reordered the VST3 attach path so the bypass flips right after the window opens; rebuild and you should see `SetHostContentScaleBypassed -> true` followed by `PlatformResize` reporting `targetScale=1.0` while `render=1.5`.
- If the UI still looks blurry, grab the `RefreshPlatformScale` and `PlatformResize` logs from that run so we can confirm the swapchain is actually using the physical pixels.
