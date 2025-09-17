# Agents.md — Plan Protocol

## Principles (always on)
- SOTA quality; SSOT; SoC; DRY; TDD bias; fail-fast; structured telemetry; deterministic builds.
- Centralized, per-module logging via one root config file (language-appropriate). That config defines module levels and log sinks/paths; all code logs through the central logger—no inline constants.
- If build/SDK tools are missing, do not block: operate in Code-Only Mode and deliver the best possible code changes.

## Built-in Defaults (hardcoded policy)
- plan_path: root/current-plan.xml
- auto_save: true
- branch_name: plan/<kebab-goal>
- branch_lock: true
- remote: origin
- base: main
- pr_mode: hold (no PRs during /proceed; only via /open-pr)
- commit_policy: checkpoint-local+push (each batch commits + pushes)
- checkpoint_cadence: per-proceed
- push_remote: true
- squash_commits: false
- decomposition_threshold: 0.90
- timebox_minutes: 15
- logging_config: discover at repo root using the codebase’s standard (e.g., logging.yaml/toml, appsettings.json, or a package.json section). Verify in t1_environment_check.

## Commands
- /new-plan — Create/overwrite the plan in the workspace (no source edits). Always add an environment check first and a final check last. Persist immediately to plan_path; set/keep the working branch; reply with a Pre-Proceed Confidence Echo.
- /proceed — Execute next safe leaf tasks. If confidence < 0.90 or heuristics trigger, split first; parent waits. Batch work, update statuses/<curcialInfo>/<tryCount>, persist, then reply with a Post-Proceed Confidence Echo (not the full plan).
- /revise — Restructure the plan (split/merge/rename/reorder; add acceptance/PROOF gates); keep history; persist; reply with a Pre-Proceed Confidence Echo focused on plan changes.
- /edit-plan — Conversationally evolve the plan (no execution); apply edits; persist; reply with a Pre-Proceed Confidence Echo.
- /save-plan — Persist plan (and optional curcialInfo bundle) to repo; reply with a Pre-Proceed Confidence Echo.
- /checkpoint — Make a local commit then push to <remote>/<branch>. Options: message:"…", allow_empty:true|false (default:false). Reply with a Post-Proceed Confidence Echo.
- /open-pr — Open or update one PR from the working branch to <vcs>/<base>. Options: title:"…", body:"…", draft:true|false (default:true), squash:false|true (default:false). Reply with a Pre-Proceed Confidence Echo (PR status).
- /delete-plan — Remove plan (and style). Reply with a short confirmation.
- /ignore-plan — Answer directly and append a note to the existing plan under <inbox> with timestamp; persist. Reply with a Pre-Proceed Confidence Echo summarizing the note capture.

## Start-of-Run Integrity Check (mandatory)
At the beginning of every /proceed:
1) git fetch --all --prune
2) Ensure branch equals <vcs>/<branch>; if not, git checkout -B <branch> origin/<branch> (or create if missing).
3) Verify <vcs>/<last_head> is an ancestor of HEAD (git merge-base --is-ancestor). If not, set the active task to RETRY and record divergence in <curcialInfo>.
4) Working tree must be clean; otherwise commit/stash and record actions in <curcialInfo>.

## End-of-Run Continuity (mandatory)
After each batch inside /proceed:
1) git add -A && git commit -m "<concise batch message>"
2) git push --no-verify --follow-tags origin <branch>
3) Write HEAD sha to <vcs>/<last_head> and persist the plan (auto_save:true).
PRs are never opened here; only via /open-pr.

## Operator Confidence Echo (required summaries in chat — strict)
Echoes appear in chat (not in the plan). They prove continuity across workers so the operator knows prior commits are preserved.

Echo enforcement
- Every reply to /new-plan, /proceed, /revise, /edit-plan, /save-plan, /checkpoint, /open-pr, and /ignore-plan MUST include the required echo. No exceptions.
- If the echo cannot be fully computed (e.g., remote not reachable), emit a minimal echo with the failure reason and current local HEAD; do not skip. If omitted accidentally, send a second message with the echo immediately.

Pre-Proceed Confidence Echo (after any command except /proceed)
- Branch: <vcs>/<branch>; Local HEAD: <sha>; Remote HEAD: <remote/branch sha or none>; Plan last_head: <sha> (ancestor: yes/no)
- Workspace: clean/dirty; unpushed commits: N
- Plan status: total tasks M; transitions since last proceed; first UNFINISHED/OPEN: <task>
- Plan edits this turn (if any): added/updated/removed tasks; codingStyle/logging changes
- Environment mode: Code-Only or Full Build; logging config path (if set)
- Next ready leaf: <task> with 1–3 concrete next steps
- PR status: none | existing PR #id (updated) | ready-to-open suggestion
- Confidence to proceed: high/medium/low (reason)
- Timebox budget remaining for next /proceed (minutes)

Post-Proceed Confidence Echo (after /proceed or /checkpoint)
- Lineage: <previous <last_head>…> → <new HEAD>; commits this batch: N
- Files changed: X (+A/−D); top files (up to 10): path (+a/−d)
- Task results this batch: {OPEN→SUCCESS:x, OPEN→RETRY:y, …}; any new UNFINISHED with <continue-info>
- Integrity check: pass/fail (rebases/stashes/conflicts noted)
- Tests: run/skip summary (Code-Only if tools missing)
- Timeboxed: yes/no; if yes, which task marked UNFINISHED
- PR status: none | existing PR #id (updated) | ready-to-open suggestion
- Next steps: the concrete next leaf and 1–3 actions
- Confidence going forward: high/medium/low (reason)

## Schema (authoritative)
- <plan> → <Goal> | <context>? | <codingStyle>? | <vcs>? | <inbox>? | <task>+
- <inbox> → <note ts="ISO-8601">free text</note>*
- <vcs> → <branch>…</branch> | <remote>…</remote> | <base>…</base> | <last_head>…</last_head>
- <task> → <name> | <status> | <tryCount> | <curcialInfo> | <continue-info>? | <tasks>?
- Status: OPEN | RETRY | AWAIT-CHILDREN-TASK-SUCCESS | PROOF | SUCCESS | UNFINISHED
- Note: tag name is <curcialInfo> (intentional misspelling).

## Task Decomposition (short rules)
- Single-shot confidence threshold = 0.90. If confidence < threshold → split into subtasks before execution.
- Force split if any: >2 files or >200 LOC; cross-module/API; concurrency/GPU/swapchain; new build/CI; missing acceptance/input; prior RETRY.
- Subtasks are first-class; parent waits (AWAIT-CHILDREN-TASK-SUCCESS). Limit nesting to 2 unless still below threshold.

## Branch & Workspace (one plan → one branch)
- Store in <vcs>: <branch>plan/<kebab-goal></branch>, <remote>origin</remote>, <base>main</base>, <last_head>UNKNOWN</last_head>.
- Reuse the same sandbox/workspace across all /proceed runs; do not re-init or squash by default.
- Work ONLY on this branch. branch_lock:true. One PR per branch→base, opened only via /open-pr.

## Environment & Toolchain Check (required first task)
<task>
  <name>t1_environment_check</name>
  <status>OPEN</status>
  <tryCount>0</tryCount>
  <curcialInfo>
    Action: Verify toolchains/SDKs/drivers/env; run a smoke build (when available); ensure the root logging config loads and writes to the configured file path.
    Acceptance: clean configure/build; smoke render (when applicable) without validation errors; module-level switches respected; log file created at configured path.
  </curcialInfo>
</task>

## Code-Only Mode (when build tools are missing)
- Keep moving: implement changes with strong interfaces/RAII, SoC, and inline docs.
- Write/adjust tests; mark skipped/pending with TODOs to enable later.
- Prefer PROOF over SUCCESS; record compile/run assumptions in <curcialInfo>.
- Add precise resume steps in <continue-info> (files/lines, commands/flags, expected artifacts, log anchors).

## Finalization (required last)
<task>
  <name>tN_final_check</name>
  <status>OPEN</status>
  <tryCount>0</tryCount>
  <curcialInfo>
    Action: Review each prior task; reopen/add subtasks for issues so next /proceed resumes at the first open item. On second arrival, report readiness confidence.
    Acceptance: written assessment per task; follow-ups captured before reporting readiness.
  </curcialInfo>
</task>

## Timebox & Graceful Stop
- Each /proceed session is timeboxed to 15 minutes.
- On timebox hit:
  - Mark the active leaf UNFINISHED.
  - Add <continue-info> with last step, next steps, files/lines, commands/flags, log anchors, branch/commit, pending checks.
  - Persist to plan_path; run /checkpoint message:"timebox: tX …"; reply with a Post-Proceed Confidence Echo.
- Next /proceed resumes from the first UNFINISHED/OPEN leaf; convert UNFINISHED→OPEN (or create subtasks) and continue.

## Reporting & Persistence
- Keep a live plan. /new-plan always writes to plan_path (create/overwrite). /proceed, /revise, /edit-plan, /save-plan, /checkpoint, and /open-pr persist after each mutation.
- Replies always include the appropriate Confidence Echo; the plan file remains the source of truth for resume and auditing.
