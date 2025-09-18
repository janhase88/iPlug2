# AGENTS.md — Plan Protocol (Remote-Free, Git-Optional, Snapshot-Durable)


-------------------------------------------------------------------------------

## Commands
- /new-plan — Create or overwrite the plan in the workspace (echo plan in user-friendly form; not XML). Always add an environment check first and a final check last. Persist immediately to plan_path; set/keep the working branch; reply with a short confirmation.
- /proceed — Execute next safe leaf tasks. If confidence < 0.90 or heuristics trigger, split first; parent waits. Batch work, update status/curcialInfo/tryCount, persist, and reply with a brief summary.
- /revise — Restructure the plan (split/merge/rename/reorder; add acceptance/PROOF gates); keep history; persist.
- /edit-plan — Conversationally evolve the plan (no execution); apply edits; persist after each change.
- /set-style — Update <codingStyle> (tests/CI/perf/security posture, decomposition knobs, plan_path, auto_save, branch_name/lock, module logging map, persistence settings). Style can tighten rules but not disable principles. If logging_config_path is omitted, use the repo’s language-standard root config.
- /save-plan — Persist plan (and optional curcialInfo bundle) to the durable store; snapshot; if local Git is available, create one commit.
- /delete-plan — Remove plan (and style).
- /ignore-plan — Answer directly; no plan changes.

IF NONE OF ABOVE COMMANDS WAS GIVEN IGNORE THIS AGENTS.md FILE - RETURN.

IF ONE OF THE ABOVE COMMANDS WAS GIVEN YOU ARE IN COMMAND MODE. IN COMMAND MODE ALWAYS CREATE GIT DIFF + SNAPSHOT
-------------------------------------------------------------------------------

Remote-free by design. Continuity between worker turns is guaranteed by filesystem **snapshots to a durable `plan_path`** and **restore on the next turn**. Local Git is **optional** (for diffs/history only). No remotes, no PRs.

-------------------------------------------------------------------------------

## Principles (always on)
- SOTA quality; SSOT; SoC; DRY; TDD bias; fail-fast; structured telemetry; deterministic builds.
- Centralized, per-module logging via a single root config (language-appropriate). That config defines module levels and log sinks/paths. All code logs through the central logger; no inline constants.

-------------------------------------------------------------------------------

## Schema (authoritative)
    <plan> → <Goal> | <context>? | <codingStyle>? | <vcs>? | <persistence>? | <task>+

    <vcs>
      <branch>plan/<kebab-goal></branch>      <!-- logical working branch name -->
      <base>main</base>                       <!-- logical base name -->
      <head_sha></head_sha>                   <!-- optional: last local Git commit SHA; empty if Git unused -->
    </vcs>

    <persistence>
      <workspace_root>/abs/ephemeral/path</workspace_root>
      <plan_path>/abs/durable/path</plan_path>              <!-- MUST survive resets -->
      <snap_dir>${plan_path}/snapshots</snap_dir>
      <patch_dir>${plan_path}/patches</patch_dir>
      <latest_meta>${plan_path}/latest.json</latest_meta>
      <persistence_mode>snapshot-repo</persistence_mode>    <!-- fixed -->
    </persistence>

    <task> → <name> | <status> | <tryCount> | <curcialInfo> | <continue-info>? | <tasks>?

    Status: OPEN | RETRY | AWAIT-CHILDREN-TASK-SUCCESS | PROOF | SUCCESS | UNFINISHED

    Note: tag name is <curcialInfo> (intentional).

-------------------------------------------------------------------------------

## Decomposition (short rules)
- Single-shot confidence threshold: 0.90. If below, split into subtasks before execution.
- Force split if: >2 files or >200 LOC; cross-module/API; concurrency/GPU/swapchain; new build/CI; missing acceptance/input; prior RETRY.
- Subtasks are first-class; parent waits. Limit nesting to 2 unless confidence remains < 0.90.

-------------------------------------------------------------------------------

## Evolving the Plan
If a Task in the Plan becomes outdated or wrong (example: Environment check reveals build tests can't be run, but there's a task "run build tests"), then update the plan.
You are allowed to change and evolve / trim the plan based on the latest needs - if task doesnt appear valid anymore edit the plan, so it stays valid.

-------------------------------------------------------------------------------

## Branch & Workspace (one plan → one branch)
- Store once in <vcs>: <branch>plan/<kebab-goal></branch>, <base>main</base>.
- Work ONLY on this logical branch. branch_lock defaults to true.
- One integration per branch → <base> (local-only; no remotes).

-------------------------------------------------------------------------------

## Durability Prerequisite (hard gate)
- All commands require a durable `plan_path`. If durability cannot be verified, commands **fail fast** with status RETRY and open `t_no_persistence`.
- Verification:
  - At Start of /proceed, ensure `plan_path` exists and is writable; create `plan_path/.persistence_sentinel` if missing.
  - If this is not the first session and the sentinel is missing, treat as “no persistence detected” and fail fast.

-------------------------------------------------------------------------------

## Persistence Model (gitless continuity; Git optional)
- Source of truth for continuity: **snapshots** under `${plan_path}/snapshots`.
- At End of /proceed (and on timebox): create a compressed snapshot of the entire `workspace_root` (including hidden files) into `snap_dir/<timestamp>.tgz`; write/update `latest_meta` with `{ snapshot_id, created_at, batch_summary, head_sha? }`.
- Also write a human-readable **unified diff** vs the previous snapshot to `patch_dir/<timestamp>.patch` for audit. If local Git is present, you may additionally write `git diff` output.
- At Start of /proceed: if `workspace_root` is empty or reset, **restore** the latest snapshot into `workspace_root` before any work begins.

-------------------------------------------------------------------------------

## Environment & Toolchain Check (required first task)
    <task>
      <name>t1_environment_check</name>
      <status>OPEN</status>
      <tryCount>0</tryCount>
      <curcialInfo>
        Action: Verify toolchains/SDKs/drivers/env; run a smoke build/run appropriate to the project; ensure the root logging config loads and writes to the configured file path; verify durability of plan_path; ensure snapshot/patch directories exist. Update this plan.
        Acceptance (if build tests are applicable): clean build; smoke run/render without validation errors; module-level switches respected; log file created at configured path; plan_path exists, is writable, and sentinel is present; snap_dir/patch_dir/latest_meta created; if local Git exists, repo initialized and on            <vcs>/<branch>.
      
      </curcialInfo>
    </task>

-------------------------------------------------------------------------------

## Finalization (required last; two-pass “second arrival” defined)
    <task>
      <name>tN_final_check</name>
      <status>OPEN</status>
      <tryCount>0</tryCount>
      <curcialInfo>
        Action: Pass 1 → perform a written assessment per task; open follow-ups; set status to PROOF and record timestamp + summary. Pass 2 → re-run checks; emit readiness confidence; if acceptable, flip to SUCCESS.
        Acceptance: Pass 1 has written assessment and captured follow-ups; Pass 2 includes explicit readiness confidence and all follow-ups captured or scheduled.
      </curcialInfo>
    </task>

-------------------------------------------------------------------------------

## Integrity & Continuity (mandatory)

Start of /proceed (restore-first):
- Verify durability (Durability Prerequisite). If not durable: fail fast (status RETRY), open `t_no_persistence` with guidance, stop.
- If `${snap_dir}` has a snapshot and `workspace_root` is empty or flagged reset, restore the **latest** snapshot into `workspace_root`.
- Ensure logical branch `<vcs>/<branch>` metadata is set (no remote branching implied).
- Local Git is optional:
  - If Git is available: ensure a repo exists; if absent, `git init` and (optionally) record `<vcs>/<branch>` as the working name.
  - Preflight cleanliness (Git present): prefer `git stash push -u -m "preflight/<timestamp>"` over preflight commits. If a preflight commit is unavoidable, limit it to the plan file only with message `preflight/<timestamp>`.
  - If Git is not present: proceed without Git; cleanliness is managed by snapshot/restore.
- Mismatch policy:
  - If Git is present and `<vcs>/<head_sha>` is set but not an ancestor of the current `HEAD` (history rewound/replaced), fail fast:
    - Mark the current leaf RETRY.
    - Open `t_repo_divergence` with guidance to reconcile (e.g., create a recovery branch, export patches, or reset to last known snapshot).
  - If Git is absent, treat the `latest_meta.snapshot_id` as the authoritative baseline; if it is missing, create an initial snapshot at End of this session.

During /proceed:
- After every mutation to the plan, **persist** the plan file to `plan_path` (filesystem write). Do **not** snapshot yet.
- Execute leaf tasks. If confidence < 0.90, split first; parent waits.

End of /proceed (atomic persist):
- Write a short **batch summary** for this session.
- Create a **unified diff** vs the previous snapshot into `patch_dir/<timestamp>.patch` (e.g., `diff -Nur` excluding bulky caches like `node_modules`, `.venv`, `.cache` if appropriate).
- Create a **compressed snapshot** of the entire `workspace_root` (including hidden files) to `snap_dir/<timestamp>.tgz`.
- Update `latest_meta` with `{ snapshot_id, created_at, batch_summary }`. If Git is present and a commit is made, also record `head_sha`.
- If Git is present:
  - Commit cadence: **one** local commit per /proceed session (and on timebox) using a single batch message (e.g., `feat(plan): <batch>`).
  - Update `<vcs>/<head_sha>` with the new `HEAD` SHA.
- Ensure the final plan state is persisted to `plan_path`.

-------------------------------------------------------------------------------

## Timebox & Graceful Stop (hard rule)
- Each /proceed session is timeboxed to 15 minutes of work.
- On hitting the timebox:
  - Mark any in-progress leaf task as UNFINISHED.
  - Add a <continue-info> block to that task with precise resume data: last completed step; next step(s); paths/files/lines; commands/flags; log anchors; snapshot_id (and commit hash if Git present); pending checks.
  - Persist the plan to plan_path.
  - Perform the same End of /proceed steps: emit unified diff; create snapshot; update latest_meta; if Git present, make exactly one commit and update <vcs>/<head_sha>.

- On the next /proceed:
  - Resume from the first UNFINISHED or OPEN leaf.
  - Convert UNFINISHED back to OPEN (or create subtasks) and continue using <continue-info>.

-------------------------------------------------------------------------------

## Persistence
- Keep a live plan. /new-plan always writes to plan_path (create/overwrite).
- /proceed, /revise, and /edit-plan persist after each mutation.
- Snapshots guarantee cross-turn continuity; patches are for audit; local Git (if present) is for intra-session diffs/history only. No remotes anywhere.
- Responses are concise; the plan file is the source of truth.
- ALWAYS CREATE SNAPSHOTS YOU CAN CONTINUE FROM ON THE NEXT TURN - ALWAYS (MANDATORY AND CRUCIAL)
-------------------------------------------------------------------------------


## SUMMARY FILE - Per turn Action modification to Turn-Report.md:
 - on first turn / plan creation create "Turn-Report.md" in same path a plan, if existing overwrite.
 - on each turn clear file and refill (overwrite) with new Turn-Report in this format:
```
=================================================================================================
Turn Report: N
=================================================================================================
[x] Snapshots:
 - Snapshot created for this turns Diff: `YES / FAILURE' - `relevant 1-3 Lines Info` 

 - Continued From Previous Snapshot: `YES / FAILURE` - `relevant 1-3 Lines Info` 

-----------------
[x] File Overview:
 - Num files Changed: N
 - Num Files created: N
 - Num Lines Modified: N

[x]List of Files changed/created:
    - `<Path/Filename>`
    - `<Path/Filename>?`
    - `[...]`

-----------------
[x] Current plan:
 - <`Taskname` - PREVIOUS STATUS: `STATUS` / CURRENT STATUS: `STATUS`>
 - <`Taskname` - PREVIOUS STATUS: `STATUS` / CURRENT STATUS: `STATUS`>
 - <`Taskname` - PREVIOUS STATUS: `STATUS` / CURRENT STATUS: `STATUS`>
 - `[...]`

[x] Message to Operator:
  <1-5 lines of text. you can freely choose what's relevant for the operator now and what you want to tell him>
=================================================================================================
```

-------------------------------------------------------------------------------


## Short Rules Recap (operational)
- Durability first: if plan_path is not durable → fail fast and open `t_no_persistence`.
- Restore snapshot before work; snapshot after work. That alone guarantees continuity.
- Persist plan after each mutation; snapshot once per session (and on timebox).
- Git optional: if present, commit once per session; prefer stash over preflight commits; never assume remotes.
- IN COMMAND MODE ALWAYS CREATE GIT DIFF + SNAPSHOT (MANDATORY , MOST IMPORTANT)
