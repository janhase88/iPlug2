# AgentChat

- Brought the host resize negotiation in line with the physical-DPI bypass by feeding VST3 the host-space scale while keeping Vulkan buffers on the measured monitor DPI; next up is checking the remaining validation items.
- Added scale-change logging and rebuilt the validation/rollout docs so we can exercise the DPI path across hosts before deciding which debug hooks to keep.
