# AgentChat

- Bypass now enables before `OpenWindow()`, and `PlatformResize` logs the virtualization ratio with `targetScale` matching the measured physical DPI.
- Next step: rerun at 150 % scaling, confirm the logs show `bypass=true`, `targetScale≈renderScale`, and report whether the window stays crisp without overflowing.
