# Agents.md — Plan Protocol (Distilled)

## Principles
- SOTA; SSOT; SoC; DRY; TDD bias; fail-fast; deterministic builds.
- Centralized logging via one root config (repo root); no inline constants.
- If tools are missing: operate in **Code-Only Mode** and still deliver best-possible code.

## Built-in Defaults
- plan_path: root/current-plan.xml · auto_save: true
- branch: plan/<kebab-goal> · branch_lock: true · remote: origin · base: main
- PRs: **hold** (only via /open-pr) · commits: **checkpoint-local+push** per /proceed
- push_remote: true · squash_commits: false · timebox_minutes: 15 · decompose_threshold: 0.90

## Commands
- /new-plan — Create/overwrite plan (no source edits). Add env check first + final check last. Persist and reply with **Pre-Proceed Echo**.
- /proceed — Execute next safe leaf; decompose if needed; persist and reply with **Post-Proceed Echo**.
- /revise, /edit-plan — Modify plan only; persist; **Pre-Proceed Echo**.
- /save-plan — Persist plan/bundles; **Pre-Proceed Echo**.
- /checkpoint — Commit then push current batch; **Post-Proceed Echo**.
- /open-pr — Open/update one PR from working branch to base; **Pre-Proceed Echo** (PR status).
- /delete-plan — Remove plan (and style).
- /ignore-plan — Answer and append a timestamped <note> under <inbox>; persist; **Pre-Proceed Echo**.

## Confidence Echo (must appear in chat every time)
**Pre-Proceed Echo** (all non-/proceed commands):
- Branch; Local HEAD; Remote HEAD; Plan <vcs>/<last_head> (ancestor: yes/no)
- Workspace: clean/dirty; unpushed commits
- Plan: totals, transitions since last proceed; first UNFINISHED/OPEN
- Any plan edits; mode (Code-Only/Full Build); logging config path
- Next ready leaf + 1–3 concrete steps; PR status; confidence; timebox budget

**Post-Proceed Echo** (/proceed, /checkpoint):
- Lineage: <prev last_head> → <new HEAD>; commits in batch
- Files changed (+A/−D) top paths; task transitions; any UNFINISHED with <continue-info>
- Integrity check (rebases/stashes/conflicts); tests summary
- Timeboxed? which task; PR status; next steps; confidence

If an echo can’t be fully computed, emit a minimal echo with the reason. If omitted, send it immediately in a follow-up before any further work.

## Integrity & Continuity (mandatory)
**Start of /proceed:** git fetch --all --prune → ensure on <vcs>/<branch> → verify <last_head> is ancestor of HEAD → ensure clean tree (commit/stash if needed).
**End of /proceed:** git add -A && git commit -m "<batch>" → git push origin <branch> → write HEAD sha to <vcs>/<last_head> → persist plan.

## Decomposition (short rules)
- If confidence < 0.90, split into subtasks first.
- Force split if any: >2 files or >200 LOC, cross-module/API, concurrency/GPU/swapchain, new build/CI, missing acceptance/input, prior RETRY.
- Parents wait with AWAIT-CHILDREN-TASK-SUCCESS. Max nesting: 2 (unless still < threshold).

## Branch & Workspace
- Single plan → single branch. Reuse the same sandbox; do not re-init or squash by default. One PR per branch, only via /open-pr.

## Required Tasks
<t1_environment_check> — verify toolchains/SDKs/driver/env; smoke build when available; root logging config loads & writes to configured path.
<tN_final_check> — review outcomes, open follow-ups as needed; on second arrival report readiness confidence.

## Timebox
- Each /proceed session is capped at 15 minutes. On hit: mark active leaf **UNFINISHED**, write <continue-info> (last step, next steps, files/lines, commands, log anchors, branch/commit, pending checks), persist, /checkpoint with “timebox: …”, then emit **Post-Proceed Echo**.

## Code-Only Mode
- Implement with strong interfaces/RAII/SoC and inline docs; write/adjust tests (mark skipped with TODOs); prefer PROOF over SUCCESS; record assumptions in <curcialInfo> and resume steps in <continue-info>.

## Schema (authoritative)
- <plan> → <Goal> | <context>? | <codingStyle>? | <vcs>? | <inbox>? | <task>+
- <inbox> → <note ts="ISO-8601">…</note>*
- <vcs> → <branch>…</branch> | <remote>…</remote> | <base>…</base> | <last_head>…</last_head>
- <task> → <name> | <status> | <tryCount> | <curcialInfo> | <continue-info>? | <tasks>?
- Status: OPEN | RETRY | AWAIT-CHILDREN-TASK-SUCCESS | PROOF | SUCCESS | UNFINISHED
- Note: tag name is <curcialInfo> (intentional misspelling).

## Reporting
- Keep the plan live in plan_path. Replies always include the required Echo. The plan file is the source of truth for resume/auditing.
