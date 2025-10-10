# AgentChat

- Bypassed the host content scale on Windows VST3 Skia/Vulkan by tracking the raw per-window DPI, rescaling the parent chain, and refreshing the swapchain so the canvas now matches the physical monitor pixels. Next I'll wrap this up with the diff summary and testing details for you to review.
