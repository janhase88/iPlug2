# AGENTS.md — Plan Execution Protocol

-------------------------------------------------------------------------------

IF NONE OF ABOVE COMMANDS WAS GIVEN IGNORE THIS AGENTS.md FILE - RETURN.

-------------------------------------------------------------------------------

## Commands
- /new-plan — Perform <Environment-Check> first, then create or overwrite the Current-Plan.xml in workspace/Repo-Name/Plan/ and create BASE-Snapshot
- /proceed — Execute next safe leaf tasks. If confidence < 0.90 or heuristics trigger, split first; parent waits. Batch work, update status/crucialInfo/tryCount, persist
- /edit-plan — Conversationally evolve the plan (no execution); apply edits; persist after each change.
- /delete-plan — Remove plan.

## Coding Principles (always on)
- SOTA quality; SSOT; SoC; DRY; TDD bias; fail-fast; structured telemetry; deterministic builds.
- Centralized, per-module logging via a single root config (language-appropriate). That config defines module levels and log sinks/paths. All code logs through the central logger; no inline constants.

## Environment
- plan_path: /workspace/Repo-Name/Plan/Current-Plan.xml
- snapshot_path: /mnt/snapshots
 
### Environment Initialize
- If plan_path or snapshot_path is not present, create the missing directories/files.

-------------------------------------------------------------------------------

## Plan Structure

<plan>
  <Goal>Main-Goal</Goal>

  <Environment-Check>
    Action: check what environment the repo needs to run build tests and if your environment supports these. Your environment is Ubuntu; Windows-based or GUI-only systems are not buildable by default.
    Acceptance: Environment supports building the repo.
    Rejection: Environment does not support building the repo.
    Set `<Build-Test-Compatible>` TRUE OR FALSE
  </Environment-Check>
  <PHASE>
   INITIAL-PLANNING | PLAN-REFINED-ONCE | PLAN-REFINED-TWICE | PLAN-EXECUTION
  </PHASE> 
  <Build-Test-Compatible>N/A</Build-Test-Compatible>
  <context>
    Main-Goal Context
  </context>

  <Tasks>

    <task>
      <name>first task</name>
      <status>OPEN | RETRY | UNFINISHED | AWAIT-CHILDREN-TASK-SUCCESS | PROOF | SUCCESS | AWAITS REVIEW | REVIEW PASSED</status>
      <tryCount>0</tryCount>
      <crucialInfo>
        important information gathered
      </crucialInfo>
      <continue-info>
        important information for the next agent to continue an interrupted task
      </continue-info>
      <subtasks>
        <task>
          [...]
        </task>
      </subtasks>
    </task>

    <task>
      <name>n task</name>
      <status>OPEN | RETRY | UNFINISHED | AWAIT-CHILDREN-TASK-SUCCESS | PROOF | SUCCESS | AWAITS REVIEW | REVIEW PASSED</status>
      <tryCount>0</tryCount>
      <crucialInfo>
        important information gathered
      </crucialInfo>
      <continue-info>
        precise resume data: last completed step; next step(s); paths/files/lines; commands/flags; log anchors; snapshot_id (and commit hash if Git present); pending checks.
      </continue-info>
      <subtasks>
        <task>
          [...]
        </task>
      </subtasks>
    </task>

  </Tasks>

  <Finalization>
    <task>
      <name>FINAL CHECK</name>
      <status>OPEN</status>
      <tryCount>0</tryCount>
      <crucialInfo>
        Action: Mark all tasks as AWAITS REVIEW. Mark this task as FINAL-REVIEW-STARTED. Review all tasks with build tests if Build-Test-Compatible = TRUE; otherwise perform a code-only perspective review. If a task fails review, reopen that task (and its subtasks) and work until it returns to AWAITS REVIEW. Loop until all tasks are REVIEW PASSED. If all tasks are REVIEW PASSED, set this task to SUCCESS: Plan successfully finished — Goal Achieved — Create Final Pull Request based on the whole Plan.
      </crucialInfo>
    </task>
  </Finalization>

</plan>

-------------------------------------------------------------------------------

## Build Tests for code changes (NOT SANDBOX WORK ENVIRONMENT)
- If Build-Test-Compatible = TRUE: obtain PROOF task status via build tests.
- If Build-Test-Compatible = FALSE: obtain PROOF task status from code-viewing only (no build tests).

-------------------------------------------------------------------------------

## Decomposition (short rules)
- Single-shot confidence threshold: 0.95. If below, split into subtasks before execution.
- Force split if: >2 files or >200 LOC; cross-module/API; concurrency/GPU/swapchain; new build/CI; missing acceptance/input; prior RETRY.
- Subtasks are first-class; parent waits. Limit nesting to 2 unless confidence remains < 0.95.

-------------------------------------------------------------------------------

## Maintaining the Plan
  If a Task in the Plan becomes outdated or wrong, update or trim it so the plan stays valid.

## Proceed Progress
  After the initial Plan Setup on next /proceed you review, refine & optimize the plan once more based on guiding principles and when done you set plan-phase to PLAN-REFINED-ONCE.
  on the next /proceed you review, refine & optimize the plan once more based on guiding principles and when done you set plan-phase to PLAN-REFINED-ONCE.
  on the next /proceed you review, refine & optimize the plan once more based on guiding principles and when done you set plan-phase to PLAN-REFINED-TWICE.
  on the next /proceed you set plan-phase to PLAN-EXECUTION and execute the tasks.

   -you move only to the next task, when a task is marked with SUCCESS.
   -for each parent-task, if it has open subtasks you start from the leaf to the root. use common state-of-art plan execution logic, when in doubt, what to do.

#Plan Logic
 - the plan should be setup in a way that it can be executed top-down, sub tasks first.

-------------------------------------------------------------------------------

## Plan Summary
(Before doing this always update plan — MANDATORY)
Create/overwrite workspace/Repo-Name/Plan/Plan-Summary.md with:

[x] Continued From Previous Snapshot: YES / NO — relevant 1–3 lines
-----------------
[x] File Overview:
- Num files Changed: N
- Num files Created: N
- Num lines Modified: N

[x] List of Files changed/created:
- Path/Filename
- Path/Filename
- [...]

-----------------
[x] Current plan:
- Taskname — PREVIOUS STATUS: STATUS / CURRENT STATUS: STATUS
- Taskname — PREVIOUS STATUS: STATUS / CURRENT STATUS: STATUS
- [...]

[x] Message to User:
1–5 lines you want the operator to see

-------------------------------------------------------------------------------

## Persistence Model
- Source of truth for continuity: git snapshots under snapshot_path.
- At End of /proceed (and on timebox): create compressed git snapshot and commit (create patch).
- At Start of /proceed: restore from latest snapshot.
- Always reply with a unified diff patch based on the difference against the original repo state and commit. 
- Always satisfy user commands by modifying repository files (with Git) instead of replying in chat.

## Persistence Model
- Source of truth for continuity: git snapshots under snapshot_path.
- At End of /proceed (and on timebox): create compressed git snapshot and commit (create patch).
- At Start of /proceed: restore from latest snapshot.
- Always reply with a unified diff patch based on the difference against the original repo state and commit. 
- Always satisfy user commands by modifying repository files (with Git) instead of replying in chat.



