# Chat Protocol

## Session 8
- Per your request I backed out the speculative Skia guards and reverted the descriptor-pool proc hook that destabilized startup, returning the Vulkan backend to the state you originally shared.
- To eliminate the shutdown validation errors at the source, I now wrap Skia's descriptor-pool creation by caching the driver's real `vkCreateDescriptorPool` per device, forcing `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` while preserving the exact function pointers Skia expects throughout initialization.
- The resolver also handles Skia's null-handle queries by remembering the active device, and it clears the cached dispatch when the Vulkan device is torn down so subsequent editors can't see stale pointers.

## Session 9
- Reworked the Vulkan descriptor-pool shim to mirror the original proc resolver behavior for non-target functions while still forcing `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`, so startup no longer sees unrelated validation noise.
- Simplified the caching logic to avoid atomics and return the wrapper only after the driver's real `vkCreateDescriptorPool` has been captured, preventing null dispatches when Skia makes its first allocation.

## Session 10
- Reinstated the stock Skia initialization flow and added a minimal descriptor-pool hook that only amends `vkCreateDescriptorPool` to set `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`, leaving every other proc lookup untouched to keep the editor stable.
- Lazily resolve and reset the driver's pool dispatcher around view lifetimes so that repeated UI openings free descriptor sets cleanly without clashing with stale Vulkan handles.
