# Windows Vulkan/Skia Scheduler Rollout Monitoring

This document describes the telemetry hooks and alert thresholds that must be wired into the existing log-scraping infrastructure before shipping the adaptive scheduler.

## 1. Log Categories
| Category | Stage | Severity Floor | Purpose |
| --- | --- | --- | --- |
| `/IGRAPHICS/SCHED/Rollout` | `runtime`, `config`, `ui` | `info` | Tracks feature-flag changes, configuration loads, and console overrides. |
| `/IGRAPHICS/SCHED/Alerts` | `ui`, `worker` | `warn` (emitted as `error`) | Signals sustained VBlank pauses or other scheduler health violations. |
| `/IGRAPHICS/SCHED/VBlankDispatch` | `worker`, `ui` | `info` | Provides context around WM_VBLANK delivery, retry counts, and health checks. |

## 2. Event Schema Highlights
- `mode_applied` / `mode_unchanged` — Emitted whenever the idle pacing mode is requested. Fields: `requested`, `effective`, `previous`, `fromConfig`, `configPath`, `changed`.
- `mode_changed` — Fired after the platform consumes the new mode. Fields: `mode`, `hwnd`, `fromConfig`.
- `config_applied` / `config_load_failed` / `config_apply_failed` — Document configuration status, allowing monitoring to flag missing or malformed JSON.
- `mode_parse_failed` / `mode_rejected_disabled` — Warn when invalid inputs are supplied or when the compile-time guard is disabled.
- `vblank_pause_alert` — Raised when six health checks fail to recover or the pause lasts ≥ 180 ms. Includes `attemptThreshold` and `durationThreshold` booleans for alert routing.

## 3. Alert Routing
- **Critical (PagerDuty / Teams):** Trigger on `/IGRAPHICS/SCHED/Alerts` events with `attemptThreshold=true` or `durationThreshold=true`.
- **Warning (Email / Dashboard):** Trigger on `/IGRAPHICS/SCHED/Rollout` events with `event=mode_rejected_disabled` or `mode_parse_failed` to catch misconfiguration.
- **Info (Dashboards):** Track counts of `mode_applied` per host to understand adoption rates.

## 4. Metrics Extraction
- Parse JSON payloads and emit the following metrics:
  - `scheduler_idle_mode_changed_total{mode,source}` incremented on `mode_applied` with `changed=true`.
  - `scheduler_vblank_pause_alert_total{reason}` incremented on `vblank_pause_alert`, with `reason` derived from `attemptThreshold` / `durationThreshold`.
  - `scheduler_config_failure_total{status}` incremented when `config_load_failed` or `config_apply_failed` fire.
- Retain rolling histograms for `sincePauseMs` and `attempt` counts to spot degraded recovery times.

## 5. Regression Dashboard Checklist
- [ ] Add `/IGRAPHICS/SCHED/Rollout` and `/IGRAPHICS/SCHED/Alerts` filters to the existing scheduler dashboard.
- [ ] Plot idle mode transitions per build over time to catch unexpected oscillations.
- [ ] Plot VBlank pause alerts per host and highlight instances exceeding 2 per hour.
- [ ] Surface the latest `config_load_failed` event with its `path` to help diagnose missing files.

## 6. Runbook Notes
- If `mode_rejected_disabled` fires, confirm the build toggled `IGRAPHICS_SCHED_IDLE_EXPERIMENTAL` to 1. Rolling back to `Legacy` mode should be immediate via console command `sched_idle_legacy`.
- When repeated `vblank_pause_alert` events trigger, capture HUD telemetry and consider disabling Adaptive pacing for the affected host using the console or configuration override.
- Always attach the latest log excerpt, HUD screenshot, and host version when filing tickets so engineers can correlate with telemetry snapshots.

