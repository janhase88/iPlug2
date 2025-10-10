# AgentChat

- I just pushed the window-resize math to log the host vs. render scales and now force the child window to follow the measured (physical) DPI whenever the bypass is active—this should eliminate the blur while keeping the parent bounds in sync.
- Rebuild the Skia/Vulkan sample and check the output window: you should now see `RefreshPlatformScale` and `PlatformResize` lines that report both the host and render scales without needing to toggle any debug switches.
