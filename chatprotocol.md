# Chat Protocol

## 2025-10-14
- **Engineer Update:** Reverted the experimental Vulkan shim that regressed startup and reinstated the repository baseline so we could reproduce the original descriptor-pool validation noise.
- **Engineer Update:** Added a focused Vulkan descriptor-pool wrapper that injects `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` while deferring every other proc lookup to the driver, preventing shutdown validation errors without disturbing startup.
- **Engineer Update:** Refined the descriptor-pool registration so we cache the driver's `vkCreateDescriptorPool` per device up front and only substitute a flag-adjusting wrapper when the lookup succeeds, ensuring teardown clears the cache cleanly for subsequent editors.
- **Engineer Update:** Simplified the Vulkan integration by restoring the baseline swapchain code and limiting the shim to a single `vkCreateDescriptorPool` wrapper that toggles the free-descriptor flag while letting all other proc lookups flow through unchanged, matching the original runtime behavior without the validation noise.
