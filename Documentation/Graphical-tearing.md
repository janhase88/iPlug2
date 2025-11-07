# Skia Vulkan VST3 VSync Audit

This report documents proven findings from reviewing the Windows Skia + Vulkan rendering path that services VST3 builds. The goal was to verify vertical synchronization (VSync) correctness, identify the cause of the horizontal tearing that was observed, and confirm the fix.

## Swap-chain configuration

* `IGraphicsWin::CreateOrResizeVulkanSwapchain` always requests `VK_PRESENT_MODE_FIFO_KHR` when it is offered by the adapter. This is the only present mode that Vulkan guarantees to be VSync-aligned, so this choice is correct with respect to Windows' VBlank cadence.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4027-L4076】
* The same routine sizes the swap-chain strictly from `VkSurfaceCapabilitiesKHR` and clamps requests to the advertised extent limits. No evidence of mis-sized back buffers or illegal usage flags was found.【F:IGraphics/Platforms/IGraphicsWin.cpp†L4070-L4094】

## Frame submission chronology

* During `IGraphicsSkia::BeginFrame`, previously acquired images are released by submitting an empty queue submission that (when presentation is required) signals `mVKRenderFinishedSemaphore`, followed by a `vkQueuePresentKHR` call that waits on that semaphore. This pathway is internally consistent.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1680-L1759】
* A fresh swap-chain image is then acquired via `vkAcquireNextImageKHR`, the image is transitioned to a renderable layout, and Skia rendering work is flushed while signalling `mVKRenderFinishedSemaphore`. The transition command buffer waits on that semaphore so GPU work does not begin before Skia finishes producing the frame.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1780-L1967】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L2080-L2143】
* `IGraphicsSkia::EndFrame` now submits the layout-to-present barrier while signalling a dedicated `mVKLayoutCompleteSemaphore`, and `vkQueuePresentKHR` waits on that semaphore before the image is scanned out. This guarantees that presentation is blocked until the GPU has completed both Skia’s rendering work and the layout transition.【F:IGraphics/Drawing/IGraphicsSkia.cpp†L2230-L2294】

## Resolved defect: presentation is synchronized with render completion

* The Windows Vulkan backend now creates a third semaphore (`layoutCompleteSemaphore`) alongside the existing acquire/render semaphores and passes it to Skia, allowing the renderer to signal presentation readiness explicitly.【F:IGraphics/Platforms/IGraphicsWin.h†L42-L52】【F:IGraphics/Platforms/IGraphicsWin.cpp†L3813-L3849】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1105-L1137】
* Vulkan’s presentation rules require the presentation engine to wait on semaphores supplied via `VkPresentInfoKHR::pWaitSemaphores` to ensure earlier queue operations finish before scan-out begins.【37cc50†L19-L36】 Wiring the new semaphore into the `vkQueueSubmit`/`vkQueuePresentKHR` pair eliminates the previously proven race and prevents torn frames during FIFO-presented swaps.

## Conclusion

* VSync configuration (FIFO present mode) is correct, so the tearing was not caused by an incorrect present mode.
* Presentation is now explicitly synchronized to GPU completion through `mVKLayoutCompleteSemaphore`, eliminating the previously observed half-rendered frames.
