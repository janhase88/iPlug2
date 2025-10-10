# AgentChat

- Decoupled the HWND sizing from the physical render scale so DPI-virtualized hosts keep the correct window bounds while Vulkan still renders at the measured DPI—ready for another validation pass.
- Flip `Examples/IPlugEffect/config/IPlugEffect-win.props` to `IGRAPHICS_SKIA;IGRAPHICS_VULKAN`, rebuild the VST3 target, and load it in a >100% scaling host; DebugView will now show `RefreshPlatformScale` pairs (DBGMSG + Vulkan log) without any extra switches.
