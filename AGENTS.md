# Agents.md — Plan-Oriented Command Protocol

A compact spec for controlling a plan-driven agent using slash commands.

---

GLOBAL HARD-CODED ENGINEERING PRINCIPLES (ALWAYS ENFORCED)
- SOTA: Strive for state-of-the-art engineering quality within project constraints.
- SSOT: Single Source of Truth for configuration, constants, and environment setup.
- SoC: Separation of Concerns between platform code, rendering backend, and utilities.
- DRY: Do not repeat yourself; prefer reuse and shared helpers.
- TDD Bias: Prefer test-first or test-augmented changes; add tests around fixes/refactors.
- Fail-Fast: Validate early, assert invariants, and surface actionable errors.
- Structured Telemetry: Use structured logs and metrics; avoid ad-hoc prints.
- Determinism: Reproducible builds and benchmark runs; document seeds and flags.

These principles are immutable defaults. /set-style may ADD stricter rules but MUST NOT disable or weaken any hard-coded principle.

---

MODULE LOGGING CONTROL (CENTRALIZED SWITCHES)
- Logging MUST be switchable per module via a single centralized configuration file.
- Default file path: config/logging.json (override via /set-style logging_config_path).
- All code must route logging through a centralized logger that reads the config at startup and supports hot-reload if feasible.
- Required schema (illustrative):
  {
    "default_level": "info|warn|error|debug|trace",
    "modules": {
      "IGraphicsSkia": "debug",
      "IGraphicsWin": "info",
      "VulkanBackend": "debug"
    },
    "format": "structured-json|plain",
    "sinks": ["stderr", "file:logs/app.log"]
  }
- Tasks that introduce or modify logging MUST update the central config, NOT inline constants, and record the change in <curcialInfo>.
- Acceptance for tasks that add logging must include: module-level switch respected, no noisy logs at default settings, structured fields present (timestamp, level, module, msg, context).

---

🎛️ COMMANDS

/new-plan
Create (or replace) the current plan from a user-stated goal, or from a provided <plan>…</plan> XML body.
Usage
- Minimal: /new-plan followed by a sentence describing the goal.
- Advanced: /new-plan followed by a full <plan>…</plan> XML body (schema below).
Agent behavior
1. If a full <plan> XML is provided, validate schema and set it as the current plan.
2. If only a goal is provided:
   - If no plan exists: auto-generate a new plan using the schema below and set it as current.
   - If a plan exists: replace the existing plan with a fresh one for the new goal (use /revise or /edit-plan to evolve).
3. Auto-generated plans must include a terminal finalization task (see Finalization Task Policy).
4. Decompose into ordered <task> blocks; set initial <status> to OPEN (or AWAIT-CHILDREN-TASK-SUCCESS for orchestration parents).
5. Populate <curcialInfo> with concrete, actionable facts and next-step parameters.
6. Initialize <tryCount> to 0 for each task.
7. If a previous plan had <codingStyle> and the user did not supply a new style, carry it forward.
8. Return the plan (as XML) for transparency.

/proceed
Advance the plan by executing the next executable tasks, updating statuses, and emitting a revised plan.
Agent behavior
1. Load the latest plan.
2. Select executable tasks:
   - All leaf tasks with <status>OPEN</status> and no children.
   - Then parent tasks whose children are all SUCCESS.
3. Before executing a selected task, apply Task Decomposition Policy; split if required.
4. Perform the work or precisely simulate steps if execution isn’t possible; respect <codingStyle> and hard-coded principles.
5. Append findings to <curcialInfo>, increment <tryCount>, set a new status (SUCCESS, RETRY, PROOF, or AWAIT-CHILDREN-TASK-SUCCESS).
6. Execute in batches per Reporting & Persistence; persist plan after mutations; return updated plan.
7. If blocked, record blockers in <curcialInfo>, set RETRY, and stop.

/delete-plan
Discard the current plan and all of its state.
Agent behavior: Clear the stored plan (including <codingStyle>) and confirm deletion.

/revise
Improve the current plan without discarding it (refactor/optimize/update structure).
Agent behavior
- Merge/split tasks, rename, reorder, clarify acceptance criteria, add PROOF gates.
- Preserve history; do not reset <tryCount> or remove useful <curcialInfo>.
- Preserve <codingStyle> unless explicitly changed.

/edit-plan
Interactive edit mode to evolve and elaborate the current plan through conversation (no execution).
Agent behavior
- Interpret messages as plan mutations: add/rename/split/merge/reorder tasks; change <Goal> or <context>; adjust <codingStyle>; add acceptance criteria; set orchestration.
- Preserve history; do not increment <tryCount>.
- Return the plan after each change. Exit when another command is issued or on “done”.

/set-style
Define or update the coding style for the current plan. Stored at <codingStyle>.
Important: Hard-coded principles remain enforced and cannot be disabled by style.
Recognized keys (subset)
- logging: terse|normal|verbose
- log_format: structured-json|plain
- logging_config_path: path to central config (default config/logging.json)
- module_levels: comma-separated module:level pairs (e.g., IGraphicsSkia:debug, IGraphicsWin:info, VulkanBackend:debug)
- tests: none|unit|tdd|unit+integration
- coverage_min: integer percent
- errors: fail-fast|accumulate
- docs: none|inline|adr
- performance: normal|optimize-critical-path
- security: baseline|strict
- ci: none|basic|strict
- decompose_threshold: 0.90
- max_files_per_task: 2
- max_nesting: 2
- require_proof_gates: true
- report_cadence: per-batch|per-task
- batch_size: integer
- auto_save: true|false
- plan_path: path to persist current plan xml
- save_commit: true|false
- branch_name: plan/<kebab-goal>
- branch_lock: true|false
- pr_base: main
- workspace_root: repo root relative path
Agent behavior
- Update or create <codingStyle> in-place; if no plan exists and a goal is supplied, create a plan first.
- Style may add stricter rules but cannot disable hard-coded principles.
- Enforce centralized logging: ensure tasks apply module-level switches via logging_config_path and module_levels.

/save-plan
Persist the current plan to the repo (and optionally export curcialInfo and open a PR).
Usage examples
- /save-plan path:docs/plans/current-plan.xml
- /save-plan path:docs/plans/plan.xml artifacts:both curcialinfo_path:docs/plans/plan.curcialinfo.xml commit:true branch:plan/topic pr:true pr_title:"chore(plan): add plan" pr_body:"Initial plan + curcialInfo export"
Agent behavior
1. Require a current plan.
2. Write plan exactly as stored (XML); create folders as needed.
3. If exporting curcialInfo: write a bundle XML with one <item task="…"> per task.
4. If commit:true: honor Branch & Workspace Policy; commit on the working branch; update PR if pr:true.
5. Append audit note (paths, branch, PR link) into the first task’s <curcialInfo>.

/ignore-plan
Bypass planning and answer directly. Stored plan remains unchanged.

---

🧩 PLAN SCHEMA (AUTHORITATIVE)
Top-level container
- <plan> — exactly one <Goal>, optional <context>, optional <codingStyle>, optional <vcs>, and one or more <task> blocks.

Allowed children and structure
- <plan> → <Goal> | <context> (optional) | <codingStyle> (optional) | <vcs> (optional) | <task> …
- <task> → <name> | <status> | <tryCount> | <curcialInfo> | <tasks> (optional)
- <tasks> → one or more <task>

Notes
- The tag is intentionally spelled <curcialInfo>.
- <codingStyle> is authoritative for execution; explicit instructions in a task’s <curcialInfo> may override style for that task.

Statuses
- OPEN — ready to start
- RETRY — previous attempt failed; reason recorded in <curcialInfo>
- AWAIT-CHILDREN-TASK-SUCCESS — orchestration parent waiting on children
- PROOF — work complete pending verification/validation
- SUCCESS — acceptance criteria met

Rules
1. One <Goal> per plan; keep it short and unambiguous.
2. Deterministic, unique <name> values (prefer t1_, t2_, … prefixes).
3. Increment <tryCount> on every execution attempt that changes state or evidence.
4. Record concrete artifacts, commands, paths, errors, and decisions in <curcialInfo>.
5. Orchestration parents usually remain AWAIT-CHILDREN-TASK-SUCCESS until children are SUCCESS.
6. Never downgrade SUCCESS; create new tasks for additional changes.
7. Prefer small, testable leaves; include acceptance criteria in <curcialInfo>.
8. With <codingStyle> present, leaves should include style-driven acceptance checks (logs present, tests updated, SoC respected).

---

🔱 BRANCH & WORKSPACE POLICY
Purpose: Ensure the agent works in a single, consistent branch/workspace for a given plan.
Plan metadata
<vcs>
  <branch>plan/<kebab-goal></branch>
  <remote>origin</remote>
  <base>main</base>
</vcs>
Rules
1) Branch selection: If <vcs>/<branch> exists use it; otherwise derive plan/<kebab-goal> or honor /set-style branch_name.
2) Branch locking: If branch_lock:true, ignore conflicting branch args; switch only via /set-style branch_lock:false or /new-plan.
3) Single-branch execution: /proceed, /edit-plan, /revise operate only on <vcs>/<branch>.
4) Persistence & PRs: /save-plan commits to <vcs>/<branch>; update a single PR to <base>.
5) Conflicts: Rebase onto <base> by default; on conflicts, record files in <curcialInfo>, set RETRY, and pause.
6) Session invariants: Adding tasks in-session never changes branch.
7) Audit trail: On mutation, append time, command, files, commit hash, branch, PR link to the first task’s <curcialInfo>.

---

🧠 TASK DECOMPOSITION POLICY
Goal: Keep tasks small, verifiable, and low-risk without over-fragmentation.
Single-shot confidence
- Internal confidence ∈ [0.0, 1.0] that a task can be completed in one pass.
- Default threshold: 0.90. If confidence < threshold → decompose before execution.
Heuristics that force decomposition (any one)
- Edits across >2 files or >200 LOC (estimated)
- Cross-module/public API changes
- Concurrency, GPU/driver/device-loss, swapchain lifecycle
- New build/tooling/CI changes
- Unclear acceptance criteria or missing inputs
- Prior attempt failed (RETRY)
Subtasks are first-class
- Same schema; parent becomes AWAIT-CHILDREN-TASK-SUCCESS; each subtask has explicit acceptance criteria; limit nesting to 2 unless still below threshold.
Execution rule
- During /proceed: split, update plan, then continue only with safe leaves; otherwise return updated plan and await next /proceed.
Editing rule
- /edit-plan or /revise may merge over-fragmented leaves if combined confidence ≥ threshold and acceptance remains crisp.
Config via /set-style: decompose_threshold, max_files_per_task, max_nesting, require_proof_gates.
Audit
- Record rationale in <curcialInfo> when decomposing.

---

📌 FINALIZATION TASK POLICY
Requirement
- Every plan must contain a terminal final check task.
Canonical task (use next available ordinal if t7_* already exists)
<task>
  <name>t7_final_check</name>
  <status>OPEN</status>
  <tryCount>0</tryCount>
  <curcialInfo>
    Action: Check each prior task (except environment check) for integrity and correctness after code updates. Reopen or add subtasks as issues are discovered so work can resume from the first open item on the next /proceed. On the second arrival at this task, report confidence in code readiness.
    Acceptance criteria: Documented assessment per task with any newly opened follow-up work captured before providing readiness confidence.
  </curcialInfo>
</task>
Environment check exclusion
- Skip tasks explicitly tagged as environment checks (name contains environment_check or curcialInfo tag: env-check).
Arrival semantics
- First arrival: assess, open follow-ups; hold PROOF until follow-ups are SUCCESS.
- Second arrival: record readiness confidence (0.0–1.0); set SUCCESS if criteria met.

---

🗃️ REPORTING & PERSISTENCE POLICY
Live plan
- Keep a live, up-to-date plan in memory and on disk whenever it mutates.
Default persistence
- Unless overridden by plan_path, save to docs/plans/current-plan.xml after /new-plan, /edit-plan, /revise, or /proceed.
- If auto_save:true, saving is automatic; otherwise, use /save-plan.
Batching and cadence
- No need to report every micro-step. Execute up to batch_size leaves per /proceed and then return a single updated plan snapshot.
- report_cadence: per-batch (default) or per-task.
Re-entry
- Always resume from the saved plan; execution never continues outside an explicit /proceed.

---

✅ VALID EXAMPLE PLAN
(The canonical example from the spec remains valid; append the finalization task using the next available ordinal.)
