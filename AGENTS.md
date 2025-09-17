# Agents.md — Minimal Plan Protocol

## Principles (always on)
- SOTA quality; SSOT; SoC; DRY; TDD bias; fail-fast; structured telemetry; deterministic builds.
- Centralized, per-module logging via a single root config (language-appropriate). That config defines module levels and log sinks/paths. All code logs through the central logger; no inline constants.

## Commands
- /new-plan — Create/overwrite the plan **in the workspace** (no plan echo). Always add an environment check as the first task and a final check as the last. Persist immediately to plan_path, set/keep the working branch, then return a short confirmation (path + branch).
- /proceed — **Execute next safe leaf tasks**. If confidence < 0.90 or heuristics trigger, first split into subtasks; parent waits (AWAIT-CHILDREN-TASK-SUCCESS). Batch work, update statuses/curcialInfo/tryCount, persist the plan, and reply with a brief summary (not the full plan).
- /revise — Restructure the plan (split/merge/rename/reorder; add acceptance/PROOF gates); keep history; persist.
- /edit-plan — Conversationally evolve the plan (no execution); apply edits; persist after each change.
- /set-style — Update <codingStyle> (tests/CI/perf/security posture, decomposition knobs, plan_path, auto_save, branch_name/lock, module logging map). Style can tighten rules but may not disable principles. If logging_config_path is omitted, use the repo’s language-standard root config.
- /save-plan — Persist plan (and optional curcialInfo bundle) to repo; commit/PR if requested.
- /delete-plan — Remove plan (and style).
- /ignore-plan — Answer directly; no plan changes.

## Schema (authoritative)
- <plan> → <Goal> | <context>? | <codingStyle>? | <vcs>? | <task>+
- <task> → <name> | <status> | <tryCount> | <curcialInfo> | <tasks>?
- Status: OPEN | RETRY | AWAIT-CHILDREN-TASK-SUCCESS | PROOF | SUCCESS
- Note: tag name is <curcialInfo> (intentional).

## Decomposition (short rules)
- Single-shot confidence threshold: 0.90. If below, split into subtasks before execution.
- Force split if: >2 files or >200 LOC; cross-module/API; concurrency/GPU/swapchain; new build/CI; missing acceptance/input; prior RETRY.
- Subtasks are first-class; parent waits. Limit nesting to 2 unless still below threshold.

## Branch & Workspace (one plan → one branch)
- Store once in <vcs>: <branch>plan/<kebab-goal></branch>, <remote>origin</remote>, <base>main</base>.
- Work ONLY on this branch. branch_lock defaults to true. One PR per branch→base.

## Required tasks
### Environment & Toolchain Check (first)
<task>
  <name>t1_environment_check</name>
  <status>OPEN</status>
  <tryCount>0</tryCount>
  <curcialInfo>
    Action: Verify toolchains/SDKs/drivers/env, run a smoke build, ensure the root logging config loads and writes to the configured file path.
    Acceptance: clean build; smoke render without validation errors; module-level switches respected; log file created at configured path.
  </curcialInfo>
</task>

### Finalization (last)
<task>
  <name>tN_final_check</name>
  <status>OPEN</status>
  <tryCount>0</tryCount>
  <curcialInfo>
    Action: Review each prior task; open follow-ups; on second arrival, report readiness confidence.
    Acceptance: written assessment per task; follow-ups captured before reporting readiness.
  </curcialInfo>
</task>

## Reporting & Persistence
- Keep a live plan. **/new-plan always writes to plan_path** (create/overwrite). /proceed, /revise, and /edit-plan persist after each mutation. Responses are concise summaries; the plan file is the source of truth for resume.
