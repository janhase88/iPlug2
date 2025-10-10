# AgentChat

- Implementation for the Windows VST3 Skia/Vulkan physical-DPI path is landed and ready for hands-on verification—flip `Examples/IPlugEffect/config/IPlugEffect-win.props` to `IGRAPHICS_SKIA;IGRAPHICS_VULKAN`, build the VST3 target in Visual Studio, and load it in a DPI-virtualized host at >100% scaling to confirm the window and rendering stay aligned.
- While validating, keep DebugView open to watch the new `RefreshPlatformScale()` logs; capture screenshots/log excerpts for the validation checklist, then we can prune or gate the debug hooks once evidence is gathered.
