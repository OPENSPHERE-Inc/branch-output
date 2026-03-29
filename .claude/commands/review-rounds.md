---
description: Automatically iterate parallel review, respond, and resolve across multiple rounds until no actionable findings remain
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*), Bash(git branch:*), Bash(mkdir:*)
---

# Automated Review Rounds

You are the **review round orchestrator**. Your job is to automatically iterate the `/parallel-review`, `/review-respond`, and `/review-resolve` flow across multiple rounds until no actionable findings remain.

**Important:** Focus exclusively on orchestration. Delegate all review, fix, and verification work to agents.

## Input

The user will specify the output directory for review documents. If the argument is `$ARGUMENTS`, interpret it as the output directory path (and options).

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--confirm-triage` | OFF | Wait for user confirmation after triage before proceeding to fixes |
| `--confirm-round` | OFF | Wait for user confirmation before proceeding to the next round when unresolved findings exist |
| `--commit` | OFF | Create a git commit after each finding fix (passed to review-respond) |
| `--max-rounds N` | 5 | Change the maximum number of outer loop rounds (1–10) |
| `--base {branch}` | `main` or `master` | Specify the base branch (passed to parallel-review) |

## Review Document File Naming

- **Format:** `{branch-name}-round{N}.md`
- **Branch name retrieval:** Get the current branch name via `git branch --show-current`.
- **Handling `/`:** When the branch name contains `/`, treat everything before the last `/` as subdirectories and the remainder as the filename prefix.
  - Example: branch `feat/add-replay` → `{output-dir}/feat/add-replay-round1.md`
  - Example: branch `fix/audio/buffer-leak` → `{output-dir}/fix/audio/buffer-leak-round1.md`
  - Example: branch `dev` → `{output-dir}/dev-round1.md`
- Create subdirectories as needed.
- Preserve all round review documents — do not overwrite.

## Review Document Language

Review documents must be written in the **user's chat language**. If the user is conversing in Japanese, output in Japanese; if in English, output in English.

## Agent Context Separation Rules

- **review, respond, and resolve must all run in separate agent contexts.** No agent reuse whatsoever.
- Agent reuse across rounds is also prohibited.
- Information sharing between agent contexts is done **only through review documents**. No verbal handoffs or context summary passing.

## Flow Overview

```
Round 1 Start
  ├─ [Agent A] parallel-review → round1.md generated
  ├─ Orchestrator checks for actionable findings (if none, exit)
  ├─ [Agent B] review-respond → round1.md updated
  ├─ [Agent C] review-resolve → round1.md verified
  ├─ Orchestrator checks for feedback
  │   └─ If feedback: [Agent D] review-respond → round1.md re-fixed
  │   └─ [Agent E] review-resolve → round1.md re-verified (up to 3 times)
  └─ Round 1 End
Round 2 Start (all new agent contexts)
  ├─ [Agent F] parallel-review → round2.md generated (full scope re-review)
  ├─ Orchestrator deduplicates against prior rounds
  └─ ...
```

## Step 1 — Initialization

1. Verify the output directory exists; create it if not.
2. Get the current branch name.
3. Parse options.
4. Set the round counter to 1.

## Step 2 — Round Loop

Repeat the following while the round counter is ≤ `--max-rounds`.

### 2.1 — Execute Review (parallel-review)

Launch a **new agent** to execute `/parallel-review` equivalent processing.

Regardless of the round number, always review the entire scope. Do not pass previous round review documents to the review agent. Deduplication against prior rounds is handled by the orchestrator (Step 2.2).

Agent prompt:

```
Execute a parallel code review.

Round: Round {N}
Base branch: {--base value, or default}
Review targets: Branch-specific commits and working tree changes (default review targets)
Review document language: {user's chat language}

Output the report to: {current round file path}
```

### 2.2 — Deduplication and Actionable Findings Check

Read the generated review document and:

1. **Deduplicate (Round 2+)** — Compare with the previous round's review document and exclude findings that were already reported and addressed. Remove excluded findings from the document or annotate them as `[No Action Needed] Addressed in prior round`.
2. **Check for actionable findings** — After deduplication, check whether any `[Action Required]` findings exist.
   - **No actionable findings:** Exit the loop and proceed to Step 3 (Final Report).
   - **Actionable findings exist:** Proceed to the next step.

### 2.3 — Review Response (review-respond)

Launch a **new agent** to execute `/review-respond` equivalent processing.

Agent prompt:

```
Respond to the findings in the review document.

Review document: {current round file path}

{If --confirm-triage is enabled:}
Present triage results to the user and wait for confirmation before proceeding to fixes.

{If --confirm-triage is disabled:}
Proceed to fixes after triage without waiting for user confirmation.

{If --commit is enabled:}
--commit option enabled: Create a git commit after each finding fix.
```

### 2.4 — Review Verification (review-resolve)

Launch a **new agent** to execute `/review-resolve` equivalent processing.

Agent prompt:

```
Verify the resolution status of findings in the review document.

Review document: {current round file path}

Output the verification report to the same directory as the review document.
Filename: {branch-name}-round{N}-verification.md
```

### 2.5 — Feedback Check and Re-fix Loop

Read the verification report and check for findings marked as "Feedback Required".

- **No feedback:** Proceed to round end.
- **Feedback exists:** Enter the re-fix loop (up to 3 iterations).

Re-fix loop:

1. Launch a **new agent** to re-execute review-respond. Instruct the agent to re-address findings in the review document based on the feedback.
2. Launch a **new agent** to re-execute review-resolve.
3. If feedback remains, repeat. If not resolved after 3 iterations, record as unresolved and end the round.

### 2.6 — Round End

Record round results:
- Number of actionable findings, fixes, and unresolved items

If `--confirm-round` is enabled and unresolved findings exist, wait for user confirmation before proceeding to the next round.

Increment the round counter and return to Step 2.1.

## Step 3 — Final Report

After all rounds complete, generate a final report. Filename: `{branch-name}-final-report.md`

The final report must be created **by you** by reading all round review documents and verification reports. Do not delegate to agents.

### Final Report Format

```markdown
# Code Review Final Report

**Branch:** {branch-name}
**Date:** YYYY-MM-DD
**Rounds executed:** {N}
**Termination reason:** {No actionable findings / Max rounds reached / User stopped}

## Statistics Summary

| Round | Findings | Actionable | Fixed | Unresolved | Feedback Re-fixes |
|-------|----------|------------|-------|------------|-------------------|
| Round 1 | ... | ... | ... | ... | ... |
| Round 2 | ... | ... | ... | ... | ... |
| **Total** | ... | ... | ... | ... | ... |

## All Findings and Resolution Status

### Resolved

| # | Round | Severity | Location | Finding Summary | Resolution |
|---|-------|----------|----------|-----------------|------------|
| 1 | Round 1 | Critical | file:line | Summary | Fixed — Description of fix |
| ... | ... | ... | ... | ... | ... |

### Unresolved

| # | Round | Severity | Location | Finding Summary | Status |
|---|-------|----------|----------|-----------------|--------|
| 1 | Round 2 | Major | file:line | Summary | Not resolved after feedback re-fixes |
| ... | ... | ... | ... | ... | ... |

### Determined No Action Needed

| # | Round | Severity | Location | Finding Summary | Reason |
|---|-------|----------|----------|-----------------|--------|
| 1 | Round 1 | Minor | file:line | Summary | Won't Fix — Reason |
| ... | ... | ... | ... | ... | ... |

## Recommended Future Actions

The following items were detected during this review but were not addressed due to being out of scope, acceptable risk, pre-existing code, or other reasons. Consider addressing these during future maintenance.

| # | Severity | Location | Summary | Recommendation Reason |
|---|----------|----------|---------|----------------------|
| 1 | Minor | file:line | Summary | Reason |
| ... | ... | ... | ... | ... |

## Review Document Index

| Round | Review | Verification |
|-------|--------|--------------|
| Round 1 | `{path}` | `{path}` |
| Round 2 | `{path}` | `{path}` |
```

## Step 4 — Completion Report

Report the final report path to the user and briefly convey key statistics (total findings, resolved, unresolved).
