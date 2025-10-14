# Chat Protocol

## 2025-10-14
- **Engineer Update:** Reverted the experimental Vulkan shim that regressed startup and reinstated the repository baseline so we could reproduce the original descriptor-pool validation noise.
- **Engineer Update:** Added a focused Vulkan descriptor-pool wrapper that injects `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` while deferring every other proc lookup to the driver, preventing shutdown validation errors without disturbing startup.
- **Engineer Update:** Refined the descriptor-pool registration so we cache the driver's `vkCreateDescriptorPool` per device upfront and only substitute a flag-adjusting wrapper when the lookup succeeds, ensuring teardown clears the cache cleanly for subsequent editors.
- **Engineer Update:** Simplified the Vulkan integration by restoring the baseline swapchain code and limiting the shim to a single `vkCreateDescriptorPool` wrapper that toggles the free-descriptor flag while letting all other proc lookups flow through unchanged, matching the original runtime behavior without the validation noise.
- **Engineer Update:** Reworked the descriptor-pool shim to track the active device even when Skia resolves procs through the instance path, ensuring every `vkCreateDescriptorPool` call is wrapped while falling back to the driver's function table when needed so validation stays quiet without destabilising startup.
- **Engineer Update:** Reset the codebase to the last known-good Vulkan path and reintroduced the descriptor-pool interception with a minimal shim that preserves the loader's dispatch for every API call except `vkCreateDescriptorPool`, adds the free-descriptor flag at creation time, and drops device-specific caches during teardown to keep follow-up editors stable.
