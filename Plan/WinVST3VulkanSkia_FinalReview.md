# Windows Vulkan/Skia Scheduler Final Review

## Scope
This review evaluated the plan execution work merged to date for the Windows VST3 Vulkan/Skia path. The focus was verifying that the new scheduler instrumentation and throttling logic satisfy the multi-instance objectives in the plan while keeping manual validation pathways intact.

## Findings
- **Per-instance paint throttling** – `InstancePaintBudget` now maintains independent counters, burst-cooling state, and deferred region handling so WM_PAINT pressure stays scoped to each editor. The struct exposes atomics for pending paints and invalidations alongside helpers that escalate throttling decisions and snapshot state for HUD/telemetry publication.
- **Bounded WM_VBLANK dispatching** – The shared `VBlankDispatchWorker` manages subscriptions across editors, coalesces retries with jittered scheduling, tracks queue depth high-water marks, and emits structured logs when thresholds trip. Dispatch requests capture enqueue depth, attempt counters, and timestamps that feed telemetry and warning paths.
- **Adaptive idle pacing** – `SchedulerState` ties burst cooling, idle catch-up, and forgiveness windows to the host idle timer. Transitions update cadence targets, stretch factors, and pending flush counters while logging state changes. Idle tick processing records queue depths, outstanding parameter work, and request-based forgiveness deadlines.
- **Parameter queue coordination** – Host idle callbacks forward `HostIdleTickInfo` so the scheduler can maintain pending flush counts, detect when parameter queues exceed thresholds, and emit warnings. Catch-up mode drops back to normal pacing once parameter backlog clears, and forgiveness windows prevent starvation when hosts request relief.
- **Telemetry and HUD coverage** – Scheduler snapshots capture paint budget histograms, vblank queue statistics, param-queue high watermarks, and idle stretch metrics. The FPS overlay now reports idle state, stretch factor, pending flushes, and vblank drops so manual validation scenarios can be observed without additional tooling.
- **DPI instrumentation** – New `IGraphicsWin[DPI]` / `IGraphicsSkia[DPI]` traces document host versus physical DPI, swapchain extents, and bitmap scale/drawScale pairs, while the `IGRAPHICS_SKIA_FORCE_DEVICE_SCALE_MILLIS` flag enables controlled device-pixel ratio experiments for crispness validation.【F:IGraphics/Platforms/IGraphicsWin.cpp†L58-L116】【F:IGraphics/Platforms/IGraphicsWin.cpp†L3490-L3532】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L1307-L1336】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L313-L385】【F:IGraphics/Drawing/IGraphicsSkia.cpp†L2727-L2762】

## Recommendation
All tracked tasks have been reviewed for code-completeness from a code-inspection standpoint. Proceed to implementation rollout steps once host-side validation per the checklist is scheduled, keeping `/IGRAPHICS/SCHED` telemetry enabled for regression monitoring.
