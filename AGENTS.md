# Agents.md — Minimal Plan Protocol

## Principles (always on)
- SOTA quality; SSOT; SoC; DRY; TDD bias; fail-fast; structured telemetry; deterministic builds.
- Centralized logging switches per module. A single root config file (language-appropriate) defines module levels **and the log file output path**. All code logs through the central logger; no inline constants.

## Commands
- /new-plan — Create/replace the plan from a goal or a <plan>…</plan>. If only a goal and no plan exists, auto-generate. Always include an environment check and a final check task. Show the plan; don’t execute.
- /proceed — Execute next safe leaf tasks. Decompose first if needed. Batch work, update statuses/curcialInfo/tryCount, persist, then return the updated plan.
- /revise — Improve structure: split/merge/rename/reorder; add acceptance/PROOF gates; keep history.
- /edit-plan — Conversationally evolve the plan (no execution). Apply edits and return the updated plan each time.
- /set-style — Update <codingStyle> (tests/CI/perf/security posture, decomposition knobs, plan_path, auto_save, branch_name/lock, module logging map). Style can tighten rules but not disable principles. If logging_config_path is omitted, use the language-appropriate root config chosen by the repo.
- /save-plan — Persist plan (and optional curcialInfo bundle) to repo; commit/PR if requested.
- /delete-plan — Remove plan (and style).
- /ignore-plan — Answer directly; no plan changes.

## Schema (authoritative)
- <plan> → <Goal> | <context>? | <codingStyle>? | <vcs>? | <task>+
- <task> → <name> | <status> | <tryCount> | <curcialInfo> | <tasks>?
- Status: OPEN | RETRY | AWAIT-CHILDREN-TASK-SUCCESS | PROOF | SUCCESS
- Note: tag is spelled <curcialInfo> (intentional).

## Task Decomposition (short rules)
- Internal single-shot confidence threshold = 0.90. If confidence < threshold → split into subtasks before execution.
- Force split if: >2 files or >200 LOC, cross-module/API, concurrency/GPU/swapchain, new build/CI, missing acceptance/input, or prior RETRY.
- Subtasks are first-class; parent waits (AWAIT-CHILDREN-TASK-SUCCESS). Limit nesting to 2 unless still below threshold.

## Branch & Workspace (one plan → one branch)
- Store once in <vcs>: <branch>plan/<kebab-goal></branch>, <remote>origin</remote>, <base>main</base>.
- Work ONLY on this branch. branch_lock defaults to true. One PR per branch→base.

## Environment & Toolchain Check (required first task)
Purpose — Prove workstation/CI, toolchains, SDKs, drivers, and baseline build/run.
Canonical task (earliest ordinal, e.g., t1_environment_check)
<task>
  <name>t1_environment_check</name>
  <status>OPEN</status>
  <tryCount>0</tryCount>
  <curcialInfo>
    Action: Verify compiler/toolchain; package manager; Vulkan/graphics SDK + validation layers; Skia version; GPU driver; env vars; test runner/CI; and that the **root logging config loads and writes logs to the path specified in that config**.
    Acceptance criteria: clean configure/build; sample renders without validation errors; module-level switches respected; **log file created at configured path**; CI/local script reproduces checks.
  </curcialInfo>
</task>

## Finalization task (required last)
Use next ordinal (tN_final_check). Summarize outcomes; reopen follow-ups; on second arrival report readiness confidence.
<task>
  <name>tN_final_check</name>
  <status>OPEN</status>
  <tryCount>0</tryCount>
  <curcialInfo>
    Action: Review each prior task. Reopen/add subtasks for issues so next /proceed resumes at the first open item. On second arrival, report readiness confidence.
    Acceptance criteria: Written assessment per task; follow-ups captured before reporting readiness.
  </curcialInfo>
</task>

## Reporting & Persistence
- Keep a live plan; if auto_save:true, write to plan_path after any mutation (/new-plan, /edit-plan, /revise, /proceed). Batch execution allowed; always return the updated plan snapshot.

## Centralized module logging (brief)
- One root config file in the repo defines module levels **and the log sinks** (stderr/file). **The log file path, rotation, and retention are taken from this config**; do not hardcode or scatter paths.
- Record the chosen config file path in <codingStyle> as logging_config_path. Module levels (e.g., IGraphicsSkia:debug, IGraphicsWin:info, VulkanBackend:debug) are controlled there.
