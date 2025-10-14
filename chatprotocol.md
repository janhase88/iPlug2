# Chat Protocol

## Session 8
- Per your request I backed out the speculative Skia guards and reverted the descriptor-pool proc hook that destabilized startup, returning the Vulkan backend to the state you originally shared.
- To eliminate the shutdown validation errors at the source, I now wrap Skia's descriptor-pool creation by caching the driver's real `vkCreateDescriptorPool` per device, forcing `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` while preserving the exact function pointers Skia expects throughout initialization.
- The resolver also handles Skia's null-handle queries by remembering the active device, and it clears the cached dispatch when the Vulkan device is torn down so subsequent editors can't see stale pointers.
