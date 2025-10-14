# Chat Protocol

## 2025-10-14
- **Engineer Update:** Reverted the experimental Vulkan shim that regressed startup and reinstated the repository baseline so we could reproduce the original descriptor-pool validation noise.
- **Engineer Update:** Added a focused Vulkan descriptor-pool wrapper that injects `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` while deferring every other proc lookup to the driver, preventing shutdown validation errors without disturbing startup.
