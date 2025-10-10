# AgentChat

- Pulled `IGraphics.h` into the VST3 view so the bypass toggle compiles again—please rebuild and confirm you now see `SetHostContentScaleBypassed -> true` before the first scale refresh.
- Once we verify the toggle fires, I'll use that telemetry to chase why the canvas is still blurry even when the swapchain resizes to the physical extent.
