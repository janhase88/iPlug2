[x] Continued From Previous Snapshot: YES — Continuing Vulkan/Skia hardening focus while adding implementation-phase tasking.
-----------------
[x] File Overview:
- Num files Changed: 4
- Num files Created: 0
- Num lines Modified: 69

[x] List of Files changed/created:
- Changed:
  - IGraphics/Platforms/IGraphicsWin.cpp
  - IPlug/IPlugAPIBase.cpp
  - Plan/Current-Plan.xml
  - Plan/Plan-Summary.md

-----------------
[x] Current plan:
- Map current Windows Vulkan/Skia rendering and message scheduling pipeline — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
  - Document WM message flow and paint lifecycle in IGraphicsWin — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
  - Document host integration touchpoints affecting scheduling — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
- Identify multi-instance contention risks and WM_PAINT queue pressure points — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (pressure points catalogued for refactor design)
  - Evaluate shared state usage and contention between plugin instances — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (scheduling counters, GPU caches, and host singletons captured for reference)
    - Inventory shared scheduling members in IGraphicsWin and related helpers — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
    - Trace shared GPU resource caches across Vulkan and Skia layers — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
    - Assess host integration singletons that may serialize instances — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
  - Analyze WM_PAINT/WM_VBLANK backlog scenarios and failure handling — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (fallback timing and idle drops captured for inspection)
    - Document queue saturation triggers and message loss symptoms — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (queue gating, ERROR_NOT_ENOUGH_QUOTA handling, and timer drops recorded)
    - Evaluate SendMessageTimeoutW fallback behaviour under contention — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (16 ms synchronous retry behaviour documented)
    - Analyze OnIdle pacing impact when paint backlog occurs — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (shared timer window drop conditions captured)
- Design refactoring strategy for resilient scheduling and OnIdle integration — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (paint budgeting, WM_VBLANK recovery, and adaptive OnIdle pacing documented)
  - Outline per-instance paint budgeting and queue throttling approach — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (counter lifecycle, batching tiers, and instrumentation path captured)
    - Propose per-instance counters replacing sPendingPaintCount while keeping lightweight global telemetry — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (InstancePaintBudget lifecycle and telemetry snapshot plan documented)
    - Design dirty-region batching and burst dampening heuristics — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (tiered coalescing, burst cooling, timer coordination ready for implementation)
    - Instrument paint budget decisions for review-only verification — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (structured logging, HUD overlay, telemetry snapshot histogram defined)
  - Design robust WM_VBLANK and fallback dispatch mechanism — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (bounded retry worker, queue telemetry, and pause protocol documented)
    - Define asynchronous worker model with bounded retry and jitter — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
    - Specify queue-depth telemetry and watchdog triggers — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
    - Plan fallback path when WM_VBLANK delivery must pause rendering — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
  - Integrate OnIdle pacing with rendering backpressure controls — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (idle pacing state machine, parameter queue coordination, and host opt-ins documented)
    - Model interaction between idle timer frequency and paint throttle states — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
    - Define coordination signals between rendering and parameter queues — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
    - Plan host opt-in/opt-out controls for idle pacing adjustments — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS
- Plan validation and hardening checkpoints — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS (validation telemetry matrix, scenario coverage, and rollout safeguards documented)
  - Define telemetry/logging requirements for review-only validation — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS (logging categories, severity triggers, sampling cadence, and review procedure captured)
    - List logging categories and severity levels for scheduling telemetry — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
    - Define per-instance diagnostic counters and sampling cadence — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
    - Capture guidance for reviewing telemetry during manual verification — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
  - Compile manual scenario matrix and success criteria — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS (host/DPI/GPU coverage, stress loops, and thresholds enumerated)
    - Enumerate host combinations and configuration variants — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
    - Define stress procedures for paint and idle workloads — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
    - Set pass/fail thresholds and observation checklist — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
- Outline rollout and regression safeguards — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS (feature flags, staged checkpoints, and rollback guidance published)
  - Plan feature flagging and fallback toggles — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
  - Define staging/QA validation checkpoints — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
  - Establish regression monitoring and rollback procedure — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
- Implement per-instance paint budgeting and throttling controls — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS (InstancePaintBudget now handles batching, burst cooling, structured telemetry, and the scheduler HUD overlay.)
  - Replace sPendingPaintCount with per-instance InstancePaintBudget tracking — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS (per-instance accounting active in IGraphicsWin)
  - Integrate tiered dirty-region batching and burst cooling heuristics — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS (dirty regions coalesced, full-surface drains escalated, burst cooling defers backlog)
  - Emit paint budget telemetry and debug surfaces — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS
- Implement WM_VBLANK dispatch worker and fallback controls — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS (bounded retry worker, telemetry, and VBlankPaused recovery now wired in IGraphicsWin)
  - Create VBlankDispatchWorker thread with bounded PostMessage retry — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS
  - Instrument WM_VBLANK queue depth and latency telemetry — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS (queue depth warnings, per-dispatch logs, and latency ring buffer now live)
  - Handle VBlankPaused fallback state and recovery flow — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS
- Integrate adaptive OnIdle pacing and queue coordination — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS (idle pacing, queue coupling, and host opt-ins complete; telemetry now feeds Scheduler.ParamQueueDepth and forgiveness tracking)
  - Implement SchedulerState transitions for idle pacing modes — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS (SchedulerState machine now drives burst cooling, catch-up, HUD telemetry, and idle cadence targets)
  - Coordinate parameter queue draining with render throttling — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS (HostIdleTickInfo now feeds PendingParamFlush, IdleForgiveness windows, and scheduler logging on Windows)
  - Expose host opt-in/opt-out and configuration controls — PREVIOUS STATUS: SUCCESS / CURRENT STATUS: SUCCESS (EIdlePacingMode plumbing, config reloads, and console commands now live)
- Wire telemetry channels, HUD overlays, and regression hooks — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS (logging categories normalized, telemetry sampling window active, and HUD exposes scheduler state/VBlank health)
  - Finalize centralized logging categories and severity thresholds — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
  - Implement histogram and counter sampling cadence — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
  - Render debug HUD overlays for paint and idle diagnostics — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS
- Introduce rollout controls and manual validation artifacts — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS (runtime toggle telemetry, validation checklist, and monitoring runbook published)
  - Implement feature flags and runtime toggles for scheduler rollout — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS (Rollout/Alerts log categories added with detailed mode change reporting)
  - Create manual validation checklist and reporting templates — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS (new checklist documents host coverage, HUD captures, and pass/fail template)
  - Configure regression monitoring and alert hooks — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS (monitoring doc defines alert routing, metrics extraction, and runbook actions)
- Code perspective correctness check — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS (review reopened implementation tasks, verified integration, and confirmed readiness for host validation)
  - Review per-instance paint budgeting and throttling integration — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS
  - Review WM_VBLANK dispatch worker and recovery flow — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS
  - Review adaptive OnIdle pacing and queue coordination — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS
  - Review telemetry channels, HUD overlays, and regression hooks — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS
  - Review rollout controls and validation artifacts — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS
- FINAL CHECK — PREVIOUS STATUS: OPEN / CURRENT STATUS: SUCCESS (final review complete; see FinalReview report)
- Windows scheduler regression follow-up — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS (MSVC scheduler logging build restored with helper hoists and qualified HostIdle telemetry)
  - Unblock HostIdle telemetry compilation on Windows — PREVIOUS STATUS: N/A / CURRENT STATUS: SUCCESS (guarded IGraphics include and SteadyClockMicros/AtomicMax relocation unblock Visual Studio)

[x] Message to User:
MSVC build breakers are addressed: HostIdle telemetry now compiles after qualifying the igraphics namespace and hoisting helper definitions, and the plan documents the regression follow-up. Continue with Windows validation using the existing checklist and watch upcoming builds for any lingering scheduler logging mismatches.
