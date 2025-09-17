# AGENTS.md — Plan Protocol (Remote-Free, Local Git + Durable Snapshots)

> Remote-free by design. Uses local Git for diffs/history and a filesystem snapshot for durability across resets.

---

## Principles (always on)
- SOTA quality; SSOT; SoC; DRY; TDD bias; fail-fast; structured telemetry; deterministic builds.
- Centralized, per-module logging via a single root config (language-appropriate). That config defines module levels and log sinks/paths. All code logs through the central logger; no inline constants.

---

## Commands
- /new-plan — Create/overwrite the plan in the workspace (echo plan in user friendly format back to operator – not as XML). Always add an environment check first and a final check last. Persist immediately to plan_path; set/keep the working branch; reply with a short confirmation.
- /proceed — Execute next safe leaf tasks. If confidence < 0.90 or heuristics trigger, split first; parent waits. Batch work, update statuses/curcialInfo/tryCount, persist, and reply with a brief summary.
- /revise — Restructure the plan (split/merge/rename/reorder; add acceptance/PROOF gates); keep history; persist.
- /edit-plan — Conversationally evolve the plan (no execution); apply edits; persist after each change.
- /set-style — Update <codingStyle> (tests/CI/perf/security posture, decomposition knobs, plan_path, auto_save, branch_name/lock, module logging map). Style can tighten rules but not disable principles. If logging_config_path is omitted, use the repo’s language-standard root config.
- /save-plan — Persist plan (and optional curcialInfo bundle) to repo; single local commit.
- /delete-plan — Remove plan (and style).
- /ignore-plan — Answer directly; no plan changes.

---

## Schema (authoritative)
    <plan> → <Goal> | <context>? | <codingStyle>? | <vcs>? | <task>+

    <vcs>
      <branch>plan/<kebab-goal></branch>
      <base>main</base>
      <last_head></last_head>
      <repo_root></repo_root>                 <!-- absolute path -->
      <persistence_mode>persistent-repo</persistence_mode>  <!-- persistent-repo | snapshot-repo -->
    </vcs>

    <task> → <name> | <status> | <tryCount> | <curcialInfo> | <continue-info>? | <tasks>?

    Status: OPEN | RETRY | AWAIT-CHILDREN-TASK-SUCCESS | PROOF | SUCCESS | UNFINISHED

    Note: tag name is <curcialInfo> (intentional).

---

## Decomposition (short rules)
- Single-shot confidence threshold: 0.90. If below, split into subtasks before execution.
- Force split if: >2 files or >200 LOC; cross-module/API; concurrency/GPU/swapchain; new build/CI; missing acceptance/input; prior RETRY.
- Subtasks are first-class; parent waits. Limit nesting to 2 unless confidence remains < 0.90.

---

## Evolving the Plan
- If a Task in the Plan becomes outdated or wrong (example: Environment check reveals build tests can't be run, but there's a task "run build tests"), then update the plan.

---

## Branch & Workspace (one plan → one branch)
- Store once in <vcs>: <branch>plan/<kebab-goal></branch>, <base>main</base>.
- Work ONLY on this branch. branch_lock defaults to true.
- One integration per branch → <base> (local merge when ready). No remotes.

---

## Persistence Model (remote-free)
- All durability is filesystem-based. Two supported modes:
  - persistent-repo: <repo_root> itself is on persistent storage. Local Git commits and files persist automatically.
  - snapshot-repo: workspace may reset; at End of /proceed (and on timebox) create a compressed snapshot of the entire <repo_root> (including .git) into plan_path/snapshots and update plan_path/latest.json. Start of /proceed restores the latest snapshot if the repo is missing.
- A human-readable unified diff (patch) vs previous snapshot is written to plan_path/patches for audit. Snapshots are the source of truth.

---

## Environment & Toolchain Check (required first task)
    <task>
      <name>t1_environment_check</name>
      <status>OPEN</status>
      <tryCount>0</tryCount>
      <curcialInfo>
        Action: Verify toolchains/SDKs/drivers/env; run a smoke build/run appropriate to the project; ensure the root logging config loads and writes to the configured file path.
        Acceptance: clean build; smoke run/render without validation errors; module-level switches respected; log file created at configured path; plan_path exists and is writable; local Git repo initialized at <repo_root> and on <vcs>/<branch>; <vcs>/<last_head> recorded after first commit if any.
      </curcialInfo>
    </task>

---

## Finalization (required last, two-pass “second arrival” defined)
    <task>
      <name>tN_final_check</name>
      <status>OPEN</status>
      <tryCount>0</tryCount>
      <curcialInfo>
        Action: Pass 1 → perform a written assessment per task; open follow-ups; set status to PROOF and record timestamp + summary. Pass 2 → re-run checks; emit readiness confidence; if acceptable, flip to SUCCESS.
        Acceptance: Pass 1 has written assessment and captured follow-ups; Pass 2 includes explicit readiness confidence and all follow-ups captured or scheduled.
      </curcialInfo>
    </task>

---

## Integrity & Continuity (mandatory)

Start of /proceed (local-only):
- If persistence_mode == snapshot-repo and repo missing: restore latest snapshot into <repo_root>, then switch to <vcs>/<branch>. If none exists, git init and create/switch to <vcs>/<branch>.
- Ensure <vcs>/<branch> exists and is current (create/switch if needed).
- Verify <vcs>/<last_head> is an ancestor of HEAD (or empty if first run).
  - If NOT ancestor (history rewound or replaced), fail fast:
    - Mark the current leaf RETRY.
    - Open task t_repo_divergence with guidance to reconcile (e.g., create recovery branch from HEAD or reset to last_head after exporting patches).
- Ensure clean tree. Prefer stash over preflight commits:
  - Stash unrelated dirt with standardized message preflight/<timestamp>.
  - If a preflight commit is unavoidable, limit it to the plan file only with message preflight/<timestamp>.
- Record current HEAD into memory for this session’s batch label.

During /proceed:
- After each mutation to the plan, persist the plan to plan_path (filesystem write). Do not commit yet.

End of /proceed (local-only):
- Single batch commit for this session: git add -A && git commit -m "<batch>".
- Write HEAD SHA to <vcs>/<last_head>.
- If persistence_mode == snapshot-repo: create a compressed snapshot of the entire <repo_root> (including .git) into plan_path/snapshots/<timestamp>.tgz; write plan_path/latest.json with snapshot_id, created_at, head_sha, and batch summary; generate a unified diff vs previous snapshot into plan_path/patches/<timestamp>.patch.
- Ensure the final plan state is persisted to plan_path.

---

## Timebox & Graceful Stop (hard rule)
- Each /proceed session is timeboxed to 15 minutes of work.
- On hitting the timebox:
  - Mark any in-progress leaf task as UNFINISHED.
  - Add a <continue-info> block to that task with precise resume data: last completed step; next step(s); paths/files/lines; commands/flags; log anchors; branch/commit hash; pending checks.
  - Persist the plan to plan_path.
  - Perform the same End of /proceed steps: single batch commit; update <vcs>/<last_head>; if snapshot-repo, snapshot and write diff.

- On the next /proceed:
  - Resume from the first UNFINISHED or OPEN leaf.
  - Convert UNFINISHED back to OPEN (or create subtasks) and continue using <continue-info>.

---

## Reporting & Persistence (explicit persist vs commit)
- The plan is a live document:
  - Persist (write to plan_path) immediately after every plan mutation made by /new-plan, /proceed, /revise, or /edit-plan.
  - Commit changes exactly once per /proceed session at End of /proceed (and upon timebox), using a single batch message.
- Snapshots (when snapshot-repo) ensure durability across environment resets; commits provide intra-session history and diffs. No remotes are used anywhere.
- Responses are concise; the plan file is the source of truth.

---
