# AgentChat

- Window/layout sizing now stays at the host DPI while the renderer logs the virtualization ratio; `DrawResize` also dumps logical vs. render dimensions so we can trace the pipeline.
- Next step: rerun at 150 % scaling, confirm `bypass=true`, `targetScale≈host`, `ratio≈physical/host`, share the new `DrawResize` lines, and tell me if the image is finally crisp inside the window.
