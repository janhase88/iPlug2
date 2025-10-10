# AgentChat

- HWND sizing now follows the measured physical DPI when the bypass is active, while the logs keep reporting the host scale so we can see the render/host split end to end.
- Next step: rerun at 150 % scaling, confirm `bypass=true`, `window`/`target`≈physical with `ratio≈physical/host`, share the new `DrawResize` lines, and tell me if the image now fills the window crisply.
