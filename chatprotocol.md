# Chat Protocol

## 2025-10-15
- **Engineer Update:** Reverted the unstable descriptor-pool experiments and reinstated the original Vulkan proc resolver so we could re-evaluate the validation failures from a clean baseline.
- **Engineer Update:** Added a targeted Vulkan descriptor-pool shim that injects `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`, tracks pool lifetimes, and safely intercepts `vkFreeDescriptorSets`/`vkDestroyDescriptorPool` calls to eliminate shutdown validation errors without disturbing Skia's startup path.
