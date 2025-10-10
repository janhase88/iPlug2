# AgentChat

- HWND sizing now follows the measured physical DPI when the bypass is active, while the logs keep reporting the host scale so we can see the render/host split end to end.
- Next step: rerun at 150 % scaling, confirm `bypass=true`, `window`/`target`≈host with `ratio≈physical/host`, share the new `DrawResize` lines, and tell me if the image now fills the window crisply.
- Thanks for the latest capture—the `DrawResize`, `RefreshPlatformScale`, and `PlatformResize` entries came through, but we were missing swapchain extents and the window scale was still following the physical DPI. I’m reworking the window-scale split so `window` stays at the host DPI and added a `SwapchainExtent` `DBGMSG`; once you rebuild, the log should include that new line alongside `window≈host`, `render≈physical`.
