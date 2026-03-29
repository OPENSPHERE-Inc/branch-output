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

## Review Document File Naming

- **Format:** `{branch-name}-round{N}.md`
- **Branch name retrieval:** Get the current branch name via `git branch --show-current`.
- **Handling `/`:** When the branch name contains `/`, treat everything before the last `/` as subdirectories and the remainder as the filename prefix.
  - Example: branch `feat/add-replay` → `{output-dir}/feat/add-replay-round1.md`
  - Example: branch `fix/audio/buffer-leak` → `{output-dir}/fix/audio/buffer-leak-round1.md`
  - Example: branch `dev` → `{output-dir}/dev-round1.md`
- Create subdirectories as needed.
- Preserve all round review documents — do not overwrite.

## Agent Context Separation Rules

- **Use completely new agent contexts per round.** Do not reuse agents from prior rounds.
- **review and resolve share the same agent context** (reviewer side), while **respond runs in a separate agent context** (fix side). This preserves role separation: the same agent that reviewed also verifies resolution.
- Information sharing between agent contexts is done **only through review documents**. No verbal handoffs or context summary passing.

## Flow Overview

```
Round 1 Start
  ├─ [Reviewer A] parallel-review → round1.md generated
  ├─ Check for actionable findings (if none, exit)
  ├─ [Fixer B] review-respond → round1.md updated
  ├─ [Reviewer A] review-resolve → round1.md verified
  ├─ Check for feedback
  │   └─ If feedback: [Fixer C] review-respond → round1.md re-fixed
  │   └─ [Reviewer A] review-resolve again (up to 3 times until no feedback)
  └─ Round 1 End
Round 2 Start (carries over unresolved findings from round1.md, new agent contexts)
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

Launch a **new reviewer-side agent** to execute `/parallel-review` equivalent processing. This agent will be reused for review-resolve in Step 2.4.

Agent prompt:

```
Execute a parallel code review.

Round: Round {N}
Review targets: Branch-specific commits and working tree changes (default review targets)

{If N > 1:}
Previous round review document: {previous round file path}
Read the previous round's review document and:
- Do not re-report findings that were already addressed.
- Focus on new issues or regressions introduced by fixes.

Output the report to: {current round file path}
```

### 2.2 — Check for Actionable Findings

Read the generated review document and check whether any `[Action Required]` findings exist.

- **No actionable findings:** Exit the loop and proceed to Step 3 (Final Report).
- **Actionable findings exist:** Proceed to the next step.

### 2.3 — Review Response (review-respond)

Launch a **new fixer-side agent** to execute `/review-respond` equivalent processing. This must run in a separate agent context from the reviewer side.

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

Use the **same reviewer-side agent from Step 2.1** to execute `/review-resolve` equivalent processing. Having the same agent that performed the review verify resolutions ensures a consistent evaluation perspective.

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

1. Launch a **new fixer-side agent** to re-execute review-respond. Instruct the agent to re-address findings in the review document based on the feedback in the verification report.
2. Use the **same reviewer-side agent** to re-execute review-resolve.
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
