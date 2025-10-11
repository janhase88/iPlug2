# Windows Vulkan/Skia Scheduler Validation Checklist

This checklist captures the manual verification loops required before enabling the adaptive scheduler for production hosts. All steps assume the new scheduler logging categories `/IGRAPHICS/SCHED/*` are enabled at `kInfo` severity (or lower) so structured JSON telemetry is emitted.

## 1. Environment Preparation
- [ ] Install host builds that exercise both Vulkan and Skia backends (e.g. Reaper x64, Cubase x64, Studio One x64).
- [ ] Ensure each DAW runs with two DPI contexts: native (100–125 %) and high-DPI (150–200 %).
- [ ] Place the `%LOCALAPPDATA%\iPlug2\IPlugSettings.json` file with the intended default `idlePacingMode` for the test run.
- [ ] Enable debug logging capture (DebugView, trace pipe, or the in-host log sink) so `/IGRAPHICS/SCHED/*` messages are persisted.

## 2. DPI Instrumentation Capture
- [ ] Build with `IGRAPHICS_DPI_LOGGING=1` (default) to capture `IGraphicsWin[DPI]` and `IGraphicsSkia[DPI]` traces.
- [ ] Verify logs show `OpenWindow`, `PlatformResize`, and `EnsureSwapchainSurface` entries listing host/physical DPI, swapchain extents, and bitmap scales.
- [ ] For forced-DPI experiments set `IGRAPHICS_SKIA_FORCE_DEVICE_SCALE_MILLIS` (e.g. `1500` for 1.5×) and confirm the logs flag `forced=1` in `DrawResize` output before proceeding to host testing.

## 3. Baseline Capture
- [ ] Launch a single plug-in instance in each host using the `Legacy` pacing mode.
- [ ] Capture 60 seconds of logs while idling to confirm `mode_changed` and `mode_applied` events surface with `Legacy`.
- [ ] Save a screenshot of the HUD overlay showing baseline counters for queued paints, idle stretch, and VBlank status.

## 4. Adaptive Mode Stress (per host, per DPI)
- [ ] Switch the instance to `Adaptive` via console command (`sched_idle_adaptive`) or configuration reload and confirm `/IGRAPHICS/SCHED/Rollout` logs a `mode_applied` event.
- [ ] Open at least three editors simultaneously. Trigger meter-heavy animations (e.g. preset morph, moving faders) for 30 seconds.
- [ ] Verify `/IGRAPHICS/SCHED/PaintBudget` and `/IGRAPHICS/SCHED/IdleState` logs show burst cooling entries while `OnIdle` events continue (HUD idle timer < 120 ms).
- [ ] Confirm `/IGRAPHICS/SCHED/VBlankDispatch` logs contain `health_check` without escalating to `/IGRAPHICS/SCHED/Alerts`.
- [ ] Record queue depth metrics from the HUD (queued invalidates, pending paints) at start, peak load, and recovery.

## 5. VBlank Pause Recovery
- [ ] Force a temporary pause by suspending the UI thread for ~250 ms (Debug -> Break All or synthetic load).
- [ ] Confirm `/IGRAPHICS/SCHED/VBlankDispatch` shows `pause`, `health_check`, and `resume` events.
- [ ] Ensure `/IGRAPHICS/SCHED/Alerts` only fires if the pause exceeds 180 ms or requires more than six health checks.
- [ ] Capture the HUD overlay before and after recovery, verifying `VBlankPaused` clears.

## 6. Locked 60 Hz Mode Regression
- [ ] Set pacing to `Locked60Hz` and repeat adaptive stress for 15 seconds.
- [ ] Confirm `/IGRAPHICS/SCHED/Rollout` logs `mode_applied` with `Locked60Hz` and that `Scheduler.IdleState` never enters burst cooling.
- [ ] Validate parameter queues drain (HUD backlog returns to zero) while paint counts remain ≤ 2.

## 7. Reporting Template
For each host/DPI combination attach the following artifacts to the validation report:
- Log excerpt (JSON or structured text) covering `mode_applied`, `health_check`, `mode_changed`, and any `/IGRAPHICS/SCHED/Alerts` events.
- HUD screenshots at idle, peak load, and post-recovery.
- Notes on perceived responsiveness, including any automation jitter or visual tearing.
- Pass/Fail verdict for each scenario with references to log timestamps.

## 8. Sign-off Gate
- [ ] Adaptive mode passes in all hosts without `/IGRAPHICS/SCHED/Alerts` firing unintentionally.
- [ ] Locked 60 Hz and Legacy behave as expected with no queue starvation.
- [ ] Documentation updated with log paths and troubleshooting steps for operators.

