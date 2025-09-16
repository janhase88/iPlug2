# Agents.md — Plan-Oriented Command Protocol

A compact spec for controlling a plan-driven agent using slash commands.

---

## 🎛️ Commands

### /new-plan
Create (or replace) the current plan from a user-stated goal, or from a provided XML plan.

Usage
- Minimal: /new-plan followed by a sentence or paragraph describing the goal.
- Advanced: /new-plan followed by a full <plan>…</plan> XML body (schema below).

Agent behavior
1. If a full <plan> XML is provided, validate schema and set it as the current plan.
2. If only a goal is provided:
   - If no plan exists: auto-generate a new plan using the schema below and set it as current.
   - If a plan exists: replace the existing plan with a fresh one for the new goal (use /revise or /edit-plan if you want to evolve instead).
3. When auto-generating, decompose into ordered <task> blocks; set initial <status> to OPEN (or AWAIT-CHILDREN-TASK-SUCCESS for orchestration parents).
4. Populate <curcialInfo> with concrete, actionable facts and next-step parameters (file paths, commands, ranges, assumptions).
5. Initialize <tryCount> to 0 for each task.
6. If a previous plan had <codingStyle> and the user did not supply a new style, carry it forward into the new plan.
7. Return the plan (as XML) for transparency.

---

### /proceed
Advance the plan by executing the next executable tasks, updating statuses, and emitting a revised plan.

Agent behavior
1. Load the latest plan.
2. Select executable tasks in this order:
   - All leaf tasks with <status>OPEN</status> and no children.
   - Then parent tasks whose children are all SUCCESS.
3. For each selected task:
   - Perform the work (or precisely simulate/describe steps if execution isn’t possible).
   - Apply any constraints from <codingStyle> (e.g., logging, tests, SoC).
   - Append findings and artifacts to <curcialInfo>.
   - Increment <tryCount>.
   - Set <status> to SUCCESS, or RETRY (with reason), or PROOF if validation is required, or AWAIT-CHILDREN-TASK-SUCCESS for orchestration.
4. If blocked, record what’s needed to unblock in <curcialInfo> and choose RETRY (or propose /revise or /edit-plan).
5. Return the updated plan.

---

### /delete-plan
Discard the current plan and all of its state.

Agent behavior
- Clear the stored plan (including <codingStyle>) and confirm deletion (no plan loaded).

---

### /revise
(Chosen single-word verb for “optimize / update / refactor plan”)

Improve the current plan without discarding it.

Agent behavior
1. Load the latest plan.
2. Apply structural improvements while preserving history:
   - Merge/split tasks for clarity.
   - Rename tasks for precision (keep semantic continuity).
   - Reorder tasks to reduce blocking/latency.
   - Convert vague tasks into concrete leaf tasks with parameters.
   - Clarify acceptance criteria and add PROOF gates.
3. Do not reset <tryCount> or remove helpful <curcialInfo>; append notes.
4. Preserve <codingStyle> unless explicitly changed.
5. Return the revised plan.

---

### /edit-plan
Enter interactive edit mode to evolve and elaborate the current plan through conversation.

Edit mode intent
- While edit mode is active, treat user messages as instructions about the plan (not requests to execute tasks). Help the user evolve the plan structure and content.

Agent behavior in edit mode
1. Load the latest plan; if none exists, request /new-plan with a goal (or auto-create if a goal is supplied with /edit-plan).
2. Interpret each user message as a plan mutation request. Examples:
   - Add/rename/split/merge/reorder tasks.
   - Change <Goal> or add clarifying constraints in <context>.
   - Add acceptance criteria, success metrics, and PROOF gates.
   - Attach concrete parameters to <curcialInfo> (paths, commands, line ranges).
   - Mark orchestration parents as AWAIT-CHILDREN-TASK-SUCCESS when applicable.
   - Convert free-form notes into leaf tasks with deterministic <name> values.
   - Adjust or extend <codingStyle>.
3. Apply requested changes, preserving history:
   - Never downgrade SUCCESS; create new tasks if additional work is needed.
   - Do not wipe <curcialInfo>; append deltas and rationale.
   - Do not reset <tryCount>; only increment on attempts during execution (/proceed), not during editing.
4. If an edit request is ambiguous, propose a precise change set and choose sensible defaults without blocking.
5. After each change, return the updated plan.

Edit mode exit conditions
- Edit mode remains active until any other command is issued (e.g., /proceed, /delete-plan, /new-plan, /revise, /ignore-plan) or the user says “done”.

---

### /save-plan
Persist the current plan to the repo (and optionally export curcialInfo and open a PR).

Usage
- Minimal: /save-plan path:docs/plans/windows-skia-vulkan.plan.xml
- With artifacts + PR:
  /save-plan path:docs/plans/windows-skia-vulkan.plan.xml artifacts:both curcialinfo_path:docs/plans/windows-skia-vulkan.curcialinfo.xml format:xml commit:true branch:plan/windows-skia-vulkan pr:true pr_title:"chore(plan): add Windows Skia–Vulkan plan" pr_body:"Initial plan + curcialInfo export"

Agent behavior
1. Require a current plan; if none, ask for /new-plan first.
2. Write the plan exactly as stored (XML). Create parent folders as needed.
3. If artifacts:curcialinfo or artifacts:both → export every <curcialInfo> node into a single file:
   - Default filename: <plan path basename>.curcialinfo.xml if curcialinfo_path is not provided.
   - XML structure:
     <curcialInfoBundle>
       <item task="t1_name"><![CDATA[...]]></item>
       ...
     </curcialInfoBundle>
   - Note: tag is spelled “curcial”, not “crucial”.
4. If commit:true and git is available:
   - Create or switch to branch (default branch: plan/<kebab-goal> if branch not provided).
   - git add written files → git commit -m "<message or default>"
5. If pr:true:
   - Push branch to origin and open a pull request.
   - PR title/body from pr_title/pr_body or sensible defaults.
6. Append the saved paths, branch name, and PR URL (if created) into a top-level audit note in the plan’s first <curcialInfo> block.

Options
- path:<file>                — where to save the plan (required)
- artifacts:none|curcialinfo|both (default:none)
- curcialinfo_path:<file>    — explicit output path for curcialinfo export
- format:xml|md              — format for plan file (default:xml)
- commit:true|false          — commit the files (default:false)
- branch:<name>              — git branch to use/create
- message:"…"                — commit message (default: chore(plan): update plan)
- pr:true|false              — open a PR (default:false)
- pr_title:"…" / pr_body:"…" — PR metadata

### /set-style
Define or update the coding style for the current plan. The style is stored inside the plan at <codingStyle> and influences how tasks are created and executed.

Usage
- Minimal: /set-style followed by short principles (e.g., “SOTA, SSOT, SoC, verbose debug, structured logs, TDD”).
- Advanced: /set-style followed by key:value lines (see schema).

Agent behavior
1. If a plan exists: update or create the <codingStyle> section in-place.
2. If no plan exists and a goal is supplied in the same message, first create a plan via /new-plan, then persist the style.
3. Style persistence:
   - /new-plan with only a goal: carry over existing <codingStyle>.
   - /new-plan with an explicit <codingStyle>: replace the previous style.
   - /delete-plan: removes the style with the plan.
4. Execution impact:
   - Logging/instrumentation rules are injected into <curcialInfo> for new tasks.
   - Testing and proof expectations add PROOF gates and acceptance criteria.
   - Architectural principles (SSOT, SoC) guide task decomposition and file boundaries.
   - Debug directives add “before/after” and “on-error” debug messages to steps.

Recognized keys (non-exhaustive)
- principles: comma-separated tags (e.g., SOTA, SSOT, SoC, KISS, DRY, YAGNI)
- logging: terse|normal|verbose
- log_format: plain|structured-json
- debug: none|basic|everywhere
- tests: none|unit|tdd|unit+integration
- coverage_min: integer percent
- errors: fail-fast|accumulate
- docs: none|inline|adr
- performance: normal|optimize-critical-path
- security: baseline|strict
- ci: none|basic|strict
- tooling: eslint+prettier|clang-format|ruff|custom:…

Example style block (to place inside <plan>)
<codingStyle>
  principles: SOTA, SSOT, SoC, DRY
  logging: verbose
  log_format: structured-json
  debug: everywhere
  tests: tdd
  coverage_min: 85
  errors: fail-fast
  docs: inline
  performance: optimize-critical-path
  security: strict
  ci: strict
</codingStyle>

---

### /ignore-plan
Bypass planning and answer directly. The stored plan, if any, remains unchanged.

Agent behavior
- Provide a direct response or action without modifying or creating a plan.

---

## 🧩 Plan Schema (authoritative)

Top-level container
- <plan> — contains exactly one <Goal>, optional <context>, optional <codingStyle>, and one or more <task> blocks.

Allowed children and required structure
- <plan> → <Goal> | <context> (optional) | <codingStyle> (optional) | <task> …
- <task> → <name> | <status> | <tryCount> | <curcialInfo> | <tasks> (optional)
- <tasks> → one or more <task> (child tasks)

Notes
- The tag name is intentionally spelled <curcialInfo> to match existing tooling.
- <codingStyle> is authoritative for execution; explicit instructions inside a task’s <curcialInfo> may override style for that task if stated.

Statuses
- OPEN — Task defined and ready to start.
- RETRY — Previous attempt failed; reason must be recorded in <curcialInfo>.
- AWAIT-CHILDREN-TASK-SUCCESS — Orchestration task waiting for all child tasks to succeed.
- PROOF — Work complete pending verification/validation.
- SUCCESS — Task meets acceptance criteria.

Rules
1. One <Goal> per plan; keep it short and unambiguous.
2. Use deterministic, unique <name> values (suggest kebab-case or snake_case with prefixes like t1_, t2_).
3. Increment <tryCount> on every attempt that changes state or evidence (execution only).
4. Write concrete artifacts, file ops, commands, error messages, and decisions into <curcialInfo>.
5. Parent tasks that only coordinate children should usually be AWAIT-CHILDREN-TASK-SUCCESS until all children are SUCCESS.
6. Once a task is SUCCESS, never downgrade it; create new tasks for additional changes.
7. Prefer small, testable leaf tasks; attach acceptance criteria in <curcialInfo>.
8. When <codingStyle> exists, new tasks should include style-driven acceptance criteria (e.g., “logs present”, “tests updated”).

---

## 🔁 Lifecycle Algorithm (summary)
0. Optionally set style with /set-style → persists in plan and influences execution.
1. Create with /new-plan → produce initial structured plan (auto-create if only a goal is provided and no plan exists; carry over <codingStyle> if present).
2. Edit with /edit-plan → collaboratively evolve the plan via conversational changes; no execution.
3. Iterate with /proceed → execute leaf tasks, update statuses, record evidence; enforce <codingStyle>.
4. Improve with /revise → restructure and clarify without losing history.
5. Reset with /delete-plan → clear state (including style).
6. Bypass with /ignore-plan → respond directly, no plan mutations.

---

## ✅ Valid Example Plan

<plan>
<Goal>Fix bug in function double GetCanvasHeight() in GraphicsUtils.cpp (returns hardcoded 500.0).</Goal>
<context>
<!--context the next turn needs in order to not get stuck in loops (e.g. read files, read lines, dependencies, code snippits, etc..)-->
</context>
<task>
<name>t1_analyze_source</name>
<status>SUCCESS</status>
<tryCount>1</tryCount>
<curcialInfo>
GraphicsUtils.cpp analyzed. GetCanvasHeight() at line 42 returns 500.0. Actual height is in m_actualCanvasHeight member.
<!--files created, general notes or whatever else is helpful and not a waste of tokens-->
<!--add as many lines as needed-->
</curcialInfo>
<tasks>
<task>
<name>t1_sub1_read_graphics_utils_cpp</name>
<status>SUCCESS</status>
<tryCount>1</tryCount>
<curcialInfo>
Action: Read GraphicsUtils.cpp relevant section.
File tool params: operation=read, path='src/utils/GraphicsUtils.cpp', lines='40-45'.
Result: Relevant section read. Line 42: 'return 500.0;'.
<!--files created, general notes or whatever else is helpful and not a waste of tokens-->
<!--add as many lines as needed-->
</curcialInfo>
</task>
</tasks>
</task>
<task>
<name>t2_implement_fix_for_GetCanvasHeight</name>
<status>AWAIT-CHILDREN-TASK-SUCCESS</status>
<tryCount>0</tryCount>
<curcialInfo>
Implementing fix to return m_actualCanvasHeight.
<!--files created, general notes or whatever else is helpful and not a waste of tokens-->
<!--add as many lines as needed-->
</curcialInfo>
<tasks>
<task>
<name>t2_sub1_correct_return_statement</name>
<status>RETRY</status>
<tryCount>1</tryCount>
<curcialInfo>
Attempt 1: Typo 'm_acutalCanvasHeight' in return statement caused compile error.
Action: Correct typo in the planned code modification.
Expected code change: 'return m_actualCanvasHeight;'
<!--files created, general notes or whatever else is helpful and not a waste of tokens-->
<!--add as many lines as needed-->
</curcialInfo>
</task>
<task>
<name>t2_sub2_write_fix_to_file</name>
<status>OPEN</status>
<tryCount>0</tryCount>
<curcialInfo>
Pending t2_sub1 success.
Action: Replace line 42 in GraphicsUtils.cpp with corrected code.
File tool params: operation=write, path='src/utils/GraphicsUtils.cpp', replace_lines='42', content=' return m_actualCanvasHeight; // Fixed: Returns dynamic height'
<!--files created, general notes or whatever else is helpful and not a waste of tokens-->
<!--add as many lines as needed-->
</curcialInfo>
</task>
</tasks>
</task>
<task>
<name>t3_test_GetCanvasHeight_fix</name>
<status>OPEN</status>
<tryCount>0</tryCount>
<curcialInfo>
Pending t2 success. Conceptual test: Verify UI elements dependent on canvas height render correctly. Check logged height value after fix.
<!--files created, general notes or whatever else is helpful and not a waste of tokens-->
<!--add as many lines as needed-->
</curcialInfo>
</task>
</plan>

---

## 🗂️ New Plan Template (copy/paste)

<plan>
<Goal><!-- one-line goal --></Goal>
<context>
<!-- artifacts, paths, constraints, env info, logs -->
</context>
<codingStyle>
<!-- optional; see /set-style keys -->
<!-- example: principles: SOTA, SSOT, SoC; logging: verbose; debug: everywhere; tests: tdd -->
</codingStyle>
<task>
<name>t1_analyze_…</name>
<status>OPEN</status>
<tryCount>0</tryCount>
<curcialInfo>
Action: …
Acceptance criteria: …
</curcialInfo>
<tasks>
<!-- optional child tasks -->
</tasks>
</task>
<!-- more tasks as needed -->
</plan>

---

## ✍️ Writing good <curcialInfo>
- Log exact operations (file paths, line ranges, commands, parameters).
- Capture errors and why a RETRY is needed.
- Define acceptance criteria or test steps for PROOF/SUCCESS.
- When <codingStyle> exists, add checks (e.g., “logs present”, “tests updated”, “SoC respected”).
- Append; don’t overwrite. Preserve a lightweight audit trail.

---

## ✅ Consistency Notes
- Schema, commands, and example align (the example omits <codingStyle>, which remains optional).
- /new-plan auto-creation from a goal is defined; style is persisted across new plans unless explicitly replaced.
- /edit-plan clearly scopes conversational evolution without execution; /proceed executes and enforces style.
- /delete-plan resets everything, including style.
