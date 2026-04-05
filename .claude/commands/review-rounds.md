---
description: Automatically iterate parallel review, respond, and resolve across multiple rounds until no actionable findings remain
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*), Bash(git branch:*), Bash(mkdir:*)
---

# Automated Review Rounds

You are the **review round orchestrator**. Your role is to automatically iterate the `/parallel-review`, `/review-respond`, and `/review-resolve` workflow across multiple rounds until no actionable findings remain.

**Important:** Focus exclusively on orchestration. Delegate all review, fix, and verification work to agents.

## Input

The user will specify the output directory for review documents. If the argument is `$ARGUMENTS`, interpret it as the output directory path (and options).

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--confirm-triage` | OFF | Wait for user confirmation after triage before proceeding to fixes |
| `--confirm-round` | OFF | Wait for user confirmation before proceeding to the next round when unresolved findings exist |
| `--commit` | OFF | Create a git commit after each finding is fixed (passed to review-respond) |
| `--max-rounds N` | 5 | Maximum number of outer loop rounds (1–10) |
| `--base {branch}` | `main` or `master` | Base branch for comparison (passed to parallel-review) |

## Review Document Naming

- **Format:** `{branch-name}-round{N}.md`
- **Branch name:** Obtained via `git branch --show-current`.
- **Handling `/`:** If the branch name contains `/`, use the part before `/` as a subdirectory and the part after as the filename prefix.
  - Example: branch `feat/add-replay` → `{output-dir}/feat/add-replay-round1.md`
  - Example: branch `fix/audio/buffer-leak` → `{output-dir}/fix/audio/buffer-leak-round1.md`
  - Example: branch `dev` → `{output-dir}/dev-round1.md`
- Create subdirectories as needed.
- Retain all round review documents — do not overwrite.

## Review Document Language

Write review documents in the **user's chat language**. If the user is communicating in Japanese, write in Japanese; if in English, write in English.

## Agent Context Isolation Rules

- **Review, respond, and resolve must each run in a separate agent context.** Never reuse agents.
- Agent reuse across rounds is also prohibited.
- Information sharing between agent contexts is done **only through review documents**. No verbal handoffs or context summaries.

## Flow Overview

```
Round 1 Start
  ├─ [Agent A] parallel-review → round1.md generated
  ├─ Orchestrator checks for actionable findings (if none, exit)
  ├─ [Agent B] review-respond → round1.md updated
  ├─ [Agent C] review-resolve → round1.md verified
  ├─ Orchestrator checks feedback
  │   └─ If feedback: [Agent D] review-respond → round1.md re-fix
  │   └─ [Agent E] review-resolve → round1.md re-verify (up to 3 times)
  └─ Round 1 End
Round 2 Start (all new agent contexts)
  ├─ [Agent F] parallel-review → round2.md generated (full re-review)
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

However, **findings that have been raised in 2 or more prior rounds and rejected each time (Won't Fix / No Action Needed)** may be explicitly listed as exclusions in the review agent's prompt. This prevents the same finding from being repeatedly raised and rejected.

**Agent launch procedure:**

1. Print to console: `## Round {N} — Step 1: Parallel Review`
2. Launch a **new agent** via the Agent tool. Specify the command file and arguments explicitly in the prompt:

```
Execute a parallel code review following the instructions in the command file below.

Command file: .claude/commands/parallel-review.md
Arguments: Round {N} {append --base value if specified}

Additional instructions:
- Review target: Commits unique to the current branch and working tree changes (default review target)
- Review document language: {user's chat language}
- Write the report to the following file: {current round file path}
{if there are findings rejected in 2+ prior rounds:}
- The following findings have been repeatedly rejected in prior rounds. Exclude them from the review:
  - {summary of each finding and the round numbers where it was rejected}
```

### 2.2 — Deduplication and Actionable Findings Check

Read the generated review document and perform the following:

1. **Deduplication (Round 2+)** — Compare with prior round review documents and exclude findings that have already been reported and addressed. Either remove excluded findings from the document or annotate them with `[No Action Needed] Addressed in prior round`.
2. **Actionable findings check** — After deduplication, check whether any `[Action Required]` findings exist.
   - **No actionable findings:** Exit the loop and proceed to Step 3 (Final Report).
   - **Actionable findings exist:** Proceed to the next step.

### 2.3 — Respond (review-respond)

**Agent launch procedure:**

1. Print to console: `## Round {N} — Step 3: Review Respond`
2. Launch a **new agent** via the Agent tool. Specify the command file and arguments explicitly in the prompt:

```
Address the findings in the review document following the instructions in the command file below.

Command file: .claude/commands/review-respond.md
Arguments: {current round file path} {if --commit enabled: --commit}

Additional instructions:
{if --commit disabled:}
Do NOT create any git commits. Only modify source code. Committing is strictly prohibited.

{if --confirm-triage enabled:}
Present triage results to the user and wait for confirmation before proceeding to fixes.

{if --confirm-triage disabled:}
Proceed to fixes without waiting for user confirmation after triage.
```

### 2.4 — Verify (review-resolve)

**Agent launch procedure:**

1. Print to console: `## Round {N} — Step 4: Review Resolve`
2. Launch a **new agent** via the Agent tool. Specify the command file and arguments explicitly in the prompt:

```
Verify the resolution status of the review document following the instructions in the command file below.

Command file: .claude/commands/review-resolve.md
Arguments: {current round file path}
```

### 2.5 — Feedback Check and Re-fix Loop

Read the verification report and check for findings that require feedback.

- **No feedback needed:** Proceed to round end.
- **Feedback needed:** Enter the re-fix loop (up to 3 attempts).

Re-fix loop (up to 3 attempts):

1. Print to console: `## Round {N} — Step 5: Feedback Fix (attempt {M}/3)`
2. Launch a **new agent** via the Agent tool to re-run review-respond:

```
Re-address the findings in the review document based on verification feedback, following the instructions in the command file below.

Command file: .claude/commands/review-respond.md
Arguments: {current round file path} {if --commit enabled: --commit}

Additional instructions:
Review the feedback annotations in the review document and address the unresolved findings.
{if --commit disabled:}
Do NOT create any git commits. Only modify source code. Committing is strictly prohibited.
```

3. Print to console: `## Round {N} — Step 5: Feedback Verify (attempt {M}/3)`
4. Launch a **new agent** via the Agent tool to re-run review-resolve:

```
Verify the resolution status of the review document following the instructions in the command file below.

Command file: .claude/commands/review-resolve.md
Arguments: {current round file path}
```

5. If feedback remains, return to step 1. If unresolved after 3 attempts, record as unresolved and end the round.

### 2.6 — Round End

Record round results:
- Number of actionable findings, fixes, and unresolved items

If `--confirm-round` is enabled and unresolved findings exist, wait for user confirmation before proceeding to the next round.

Increment the round counter and return to Step 2.1.

## Step 3 — Final Report

After all rounds complete, generate a final report. Filename: `{branch-name}-final-report.md`

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
