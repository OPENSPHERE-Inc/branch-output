---
name: review-rounds
description: Automatically iterate parallel review, respond, and resolve across multiple rounds until no actionable findings remain
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*), Bash(git branch:*), Bash(mkdir:*)
---

# Automated Review Rounds

You are the **review round orchestrator**. Your role is to automatically iterate the `/parallel-review`, `/review-respond`, and `/review-resolve` workflow across multiple rounds until no actionable findings remain.

**Important:** Focus exclusively on orchestration. Delegate all review, fix, and verification work to agents.

## Input

The user may optionally specify an output base path. If the argument is `$ARGUMENTS`, interpret it as the output base path (and options). If no output base path is specified, default to `.claude/tmp/` under the project root.

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--confirm-triage` | OFF | Wait for user confirmation after triage before proceeding to fixes |
| `--confirm-round` | OFF | Wait for user confirmation before proceeding to the next round when unresolved findings exist |
| `--commit` | OFF | Create a git commit after each finding is fixed (passed to review-respond) |
| `--max-rounds N` | 5 | Maximum number of outer loop rounds (1–10) |
| `--base {branch}` | `main` or `master` | Base branch for comparison (passed to parallel-review) |

## Review Document Naming

- **Format:** `{base-path}/{branch-dir}/review-round{N}.md`
- **Branch name:** Obtained via `git branch --show-current`.
- **Branch name is used as a directory path** — the entire branch name (including `/`) becomes the directory hierarchy.
- **Sequential suffix for re-runs on the same branch:** If the directory `{base-path}/{branch-name}` already exists, append a sequential suffix: `{branch-name}_1`, `{branch-name}_2`, etc. This prevents overwriting results from previous runs.
  - To determine the suffix: check `{branch-name}_1` first, then `_2`, etc., and use the smallest number whose directory does not yet exist.
- **Examples:**
  - First run: branch `feat/add-replay` → `{base-path}/feat/add-replay/review-round1.md`
  - Second run: `feat/add-replay` already exists → `{base-path}/feat/add-replay_1/review-round1.md`
  - Third run: `feat/add-replay_1` also exists → `{base-path}/feat/add-replay_2/review-round1.md`
  - Branch `dev` → `{base-path}/dev/review-round1.md` (first run), `{base-path}/dev_1/review-round1.md` (second run)
- **Default base-path:** `.claude/tmp/`
- Create directories as needed.
- Retain all round review documents — do not overwrite.

## Review Document Language

Write review documents in the **user's chat language**. If the user is communicating in Japanese, write in Japanese; if in English, write in English.

## Subagent Usage Rules

- **Subagent nesting is prohibited** — If you are running as a subagent yourself, you cannot launch further subagents from within.
- Therefore, the following roles must be **performed by the orchestrator (you) directly**:
  - **Parallel-review role (Step 2.1)** — You may launch reviewer agents (cpp-sensei, etc.) in parallel, but do NOT delegate the entire parallel-review skill to another agent.
  - **Fix work (Step 2.3 / Step 2.5 re-fix)** — The orchestrator performs fixes directly, acting as each relevant specialist. Do NOT launch specialist subagents.
- Tasks that do **not** launch further subagents themselves may be delegated to a subagent:
  - **Triage (Step 2.3 / Step 2.5)** — Running in a separate context avoids orchestrator bias.
  - **Review-resolve verification (Step 2.4 / Step 2.5)** — Read-only verification task.
- Results from each round are passed forward **only through review documents**.

## Flow Overview

```
Round 1 Start
  ├─ Orchestrator acts as parallel-review lead, launching reviewers
  │   (cpp-sensei, qt-sensei, obs-sensei, network-sensei, etc.) in parallel
  │   → round1.md generated
  ├─ Orchestrator checks for actionable findings (if none, exit)
  ├─ [Agent A] triage (fresh context decides Will Fix / Won't Fix)
  ├─ Orchestrator applies fixes directly as specialists
  │   → round1.md updated
  ├─ [Agent B] review-resolve → round1.md verified
  ├─ Orchestrator checks feedback
  │   └─ If feedback: [Agent C] triage → orchestrator re-fixes (up to 3 times)
  │   └─ [Agent D] review-resolve → round1.md re-verified
  └─ Round 1 End
Round 2 Start
  ├─ Orchestrator launches reviewers in parallel (fresh reviewer contexts)
  │   → round2.md generated (full re-review)
  ├─ Orchestrator deduplicates against prior rounds
  └─ ...
```

## Step 1 — Initialization

1. Verify the output directory exists; create it if needed.
2. Get the current branch name.
3. Parse options.
4. Set the round counter to 1.

## Step 2 — Round Loop

Repeat while the round counter is ≤ `--max-rounds`.

### 2.1 — Review (parallel-review)

Regardless of the round, always review the entire target scope. Do not pass prior round review documents to the review agent. Deduplication against prior rounds is handled by the orchestrator (Step 2.2).

However, **findings that have been raised and rejected 2 or more times across prior rounds (Won't Fix / No Action Needed in each case)** may be explicitly listed as exclusions in the review agent's prompt.

**Important constraints:**
- **Do NOT exclude findings rejected only once.** The judgment may change on the second review, so the finding must be surfaced again.
- A finding is only eligible for exclusion when it has been repeatedly rejected across multiple rounds (**2 or more total rejections**).

**Important: Subagent nesting is prohibited**

Subagents cannot launch further subagents. Therefore, in this step, the `review-rounds` orchestrator (you) acts as the parallel-review lead directly, launching reviewer agents in parallel and consolidating the report. Follow the rules in `.claude/skills/parallel-review/SKILL.md` (without delegating to another agent).

**Execution procedure:**

1. Print to console: `## Round {N} — Step 1: Parallel Review`
2. Read `.claude/skills/parallel-review/SKILL.md` and follow its review rules (required/optional reviewers, severity classification, report format, etc.).
3. Launch reviewer agents **in parallel** via the Agent tool (cpp-sensei, qt-sensei, obs-sensei, network-sensei are required; add av-sensei, devops-sensei, python-sensei, lua-sensei as needed based on scope). Provide each reviewer with:
   - Review target: Commits unique to the current branch and working tree changes
   - Their specialist perspective
   - Read-only instruction with severity labels (Critical / Major / Minor / Info)
   - {include only if there are findings rejected 2 or more times (do NOT include findings rejected only once):}
     - Exclude these findings that have been rejected 2+ times in prior rounds: {summary of each finding and the round numbers where it was rejected}
4. Collect all reviewer results, deduplicate, annotate with `[Action Required]` / `[No Action Needed]` decisions, and write out the report following parallel-review's report format:
   - File path: {current round file path}
   - Review document language: {user's chat language}

### 2.2 — Deduplication and Actionable Findings Check

Read the generated review document and perform the following:

1. **Deduplication (Round 2+)** — Compare with prior round review documents and exclude findings that have already been reported and addressed. Either remove excluded findings from the document or annotate them with `[No Action Needed] Addressed in prior round`.
2. **Actionable findings check** — After deduplication, check whether any `[Action Required]` findings exist.
   - **No actionable findings:** Exit the loop and proceed to Step 3 (Final Report).
   - **Actionable findings exist:** Proceed to the next step.

### 2.3 — Respond (review-respond)

**Agent context separation:**

- **Triage runs in a separate agent context** — Delegate the triage step (Will Fix / Won't Fix / Needs Investigation decisions) to a dedicated subagent. This avoids bias from the orchestrator's accumulated context and produces fresher judgment.
- **Fixes are performed by the orchestrator** — After receiving triage results, the orchestrator (you) applies fixes directly, acting as the appropriate specialist. Do NOT launch specialist subagents (subagent nesting is prohibited).

Follow the rules in `.claude/skills/review-respond/SKILL.md` (delegate only the triage; orchestrator performs the fix).

**Execution procedure:**

1. Print to console: `## Round {N} — Step 3: Review Respond (Triage)`
2. Launch a **new subagent** via the Agent tool to perform triage. Example prompt:

```
Triage the findings in the review document, following the instructions in the skill file below.
**This task is triage only.** Do NOT perform fixes. Report triage results only.

Skill file: .claude/skills/review-respond/SKILL.md — "Step 2 — Triage"
Arguments: {current round file path}

For each `[Action Required]` finding, classify as one of:
- **Will Fix** — Valid finding, should be addressed.
- **Won't Fix** — Not applicable / false positive / acceptable risk (state the reason).
- **Needs Investigation** — Read the relevant source to decide → then finalize as Will Fix / Won't Fix.

Output format:
| Finding ID | Decision | Rationale |
|------------|----------|-----------|
| C-1 | Will Fix | ... |
| M-2 | Won't Fix | Pre-existing code, out of scope |
```

3. Print to console: `## Round {N} — Step 3: Review Respond (Fix)`
4. Receive the triage results. Then, **as the orchestrator**, perform the fixes under the following additional constraints:
   - Follow `.claude/skills/review-respond/SKILL.md` from Step 3 onward (fix, self-review, format verification, build verification, review document update, commit).
   - Arguments: {current round file path} {if --commit enabled: --commit}
   - **You** apply fixes directly from the perspective of the appropriate specialist (cpp-sensei / qt-sensei / obs-sensei / network-sensei / av-sensei / devops-sensei / python-sensei / lua-sensei). Do NOT delegate to another agent.
   - {if --commit disabled:} Do NOT create any git commits. Only modify source code. Committing is strictly prohibited.
   - {if --confirm-triage enabled:} Present the triage results to the user and wait for confirmation before proceeding to fixes.
   - {if --confirm-triage disabled:} Proceed to fixes without waiting for user confirmation after triage.

### 2.4 — Verify (review-resolve)

**Agent launch procedure:**

1. Print to console: `## Round {N} — Step 4: Review Resolve`
2. Launch a **new agent** via the Agent tool. Specify the skill file and arguments explicitly in the prompt:

```
Verify the resolution status of the review document following the instructions in the skill file below.

Skill file: .claude/skills/review-resolve/SKILL.md
Arguments: {current round file path}
```

### 2.5 — Feedback Check and Re-fix Loop

Read the verification report and check for findings that require feedback.

- **No feedback needed:** Proceed to round end.
- **Feedback needed:** Enter the re-fix loop (up to 3 attempts).

Re-fix loop (up to 3 attempts):

1. Print to console: `## Round {N} — Step 5: Feedback Triage (attempt {M}/3)`
2. Launch a **new subagent** via the Agent tool to triage the unresolved findings based on feedback annotations. Example prompt:

```
Triage the unresolved findings in the review document, taking feedback annotations into account.
**This task is triage only.** Do NOT perform fixes. Report triage results only.

Skill file: .claude/skills/review-respond/SKILL.md — "Step 2 — Triage"
Arguments: {current round file path}

Prioritize feedback annotations (`> **{id} Feedback:** ...`) when deciding.
Output format: same as Step 2.3.
```

3. Print to console: `## Round {N} — Step 5: Feedback Fix (attempt {M}/3)`
4. Receive the triage results. **As in Step 2.3, you perform re-fixes directly as the specialist** (do NOT delegate). Follow `.claude/skills/review-respond/SKILL.md` from Step 3 onward, with the following additional constraints:
   - Arguments: {current round file path} {if --commit enabled: --commit}
   - Review the feedback annotations in the review document and address the unresolved findings.
   - **You** apply fixes directly from the perspective of the appropriate specialist. Do NOT delegate to another agent.
   - {if --commit disabled:} Do NOT create any git commits. Committing is strictly prohibited.

5. Print to console: `## Round {N} — Step 5: Feedback Verify (attempt {M}/3)`
6. Launch a **new agent** via the Agent tool to re-run review-resolve:

```
Verify the resolution status of the review document following the instructions in the skill file below.

Skill file: .claude/skills/review-resolve/SKILL.md
Arguments: {current round file path}
```

7. If feedback remains, return to step 1. If unresolved after 3 attempts, record as unresolved and end the round.

### 2.6 — Round End

Record round results:
- Number of actionable findings, fixes, and unresolved items

If `--confirm-round` is enabled and unresolved findings exist, wait for user confirmation before proceeding to the next round.

Increment the round counter and return to Step 2.1.

## Step 3 — Final Report

After all rounds complete, generate a final report. File path: `{base-path}/{branch-dir}/final-report.md`

The final report must be written by **you (the orchestrator)** by reading all round review documents and verification reports. Do not delegate to an agent.

### Final Report Format

```markdown
# Code Review Final Report

**Branch:** {branch-name}
**Date:** YYYY-MM-DD
**Rounds completed:** {N}
**Termination reason:** {No actionable findings / Max rounds reached / User stopped}

## Statistics Summary

| Round | Findings | Action Required | Fixed | Unresolved | Feedback Re-fixes |
|-------|----------|-----------------|-------|------------|-------------------|
| Round 1 | ... | ... | ... | ... | ... |
| Round 2 | ... | ... | ... | ... | ... |
| **Total** | ... | ... | ... | ... | ... |

## All Findings and Resolution Status

### Resolved

| # | Round | Severity | Location | Summary | Resolution |
|---|-------|----------|----------|---------|------------|
| 1 | Round 1 | Critical | file:line | Summary | Fixed — Description of fix |
| ... | ... | ... | ... | ... | ... |

### Unresolved

| # | Round | Severity | Location | Summary | Status |
|---|-------|----------|----------|---------|--------|
| 1 | Round 2 | Major | file:line | Summary | Not resolved after feedback re-fixes |
| ... | ... | ... | ... | ... | ... |

### No Action Needed

| # | Round | Severity | Location | Summary | Reason |
|---|-------|----------|----------|---------|--------|
| 1 | Round 1 | Minor | file:line | Summary | Won't Fix — Reason |
| ... | ... | ... | ... | ... | ... |

## Recommended Future Actions

The following items were detected during this review but were not addressed due to being out of scope, acceptable risk, or pre-existing code. Consider addressing them in future maintenance.

| # | Severity | Location | Summary | Rationale |
|---|----------|----------|---------|-----------|
| 1 | Minor | file:line | Summary | Reason |
| ... | ... | ... | ... | ... |

## Review Document Index

| Round | Review | Verification |
|-------|--------|--------------|
| Round 1 | `{path}` | `{path}` |
| Round 2 | `{path}` | `{path}` |
```

## Step 4 — Completion Report

Report the final report path to the user and briefly convey the key statistics (total findings, resolved, unresolved).
