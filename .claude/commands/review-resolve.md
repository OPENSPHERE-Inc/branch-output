---
description: Verify review finding resolutions against actual source code and provide feedback
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*)
---

# Review Resolve

You are the **review verifier**. Your job is to reload an updated review document, verify each finding's resolution status against the actual source code, and report whether each finding is truly resolved or needs further work.

Verification feedback is written back to the review document (Step 4).

## Input

The user will specify a path to a review document (markdown). If the argument is `$ARGUMENTS`, interpret it as the path to the review document.

## Review Document Format

The review document uses markdown tables grouped by severity (`## Critical`, `## Major`, `## Minor`, `## Info`). Each severity section has a table with columns for finding ID, location, description, reviewer(s), and action.

Status annotations appear as blockquote lines after each table:

- `> **{id} Status: Fixed** — ...` — The finding was addressed with a code fix.
- `> **{id} Status: Won't Fix** — ...` — The finding was intentionally not fixed, with rationale.
- `> **{id} Status: Acknowledged** — ...` — A `[No Action Needed]` finding was reviewed and assessed.

## Step 1 — Reload and Parse

1. Read the entire review document.
2. Extract all findings from **Critical**, **Major**, and **Minor** sections.
3. For each finding, extract:
   - Finding ID, severity, location (file and lines), description
   - Reviewer action (`[Action Required]` or `[No Action Needed]`)
   - Current status annotation (Fixed / Won't Fix / Acknowledged / none)

## Step 2 — Verify Each Finding

For each finding that has a status annotation, verify its resolution by reading the actual source code:

### For `Status: Fixed` findings:

1. Read the referenced file(s) and line(s).
2. Confirm the described fix is actually present in the code.
3. Check whether the fix fully addresses the original issue — not just partially.
4. Check whether the fix introduced any new issues (regressions, new bugs, style violations, thread safety problems, resource leaks, etc.).
5. Verdict:
   - **Resolved** — The fix is correct, complete, and introduces no new issues.
   - **Feedback** — The fix is missing, incomplete, or introduces new problems. Describe what remains.

### For `Status: Won't Fix` findings:

1. Read the referenced file(s) to understand the current state.
2. Assess whether the "Won't Fix" rationale is still valid given the current code.
3. Verdict:
   - **Resolved** — The rationale is sound and the decision is appropriate.
   - **Feedback** — The rationale is flawed or circumstances have changed. Explain why.

### For `Status: Acknowledged` findings:

1. Read the referenced file(s) to understand the current state.
2. Assess whether the acknowledgment is appropriate and the reviewer's original `[No Action Needed]` rationale holds.
3. Verdict:
   - **Resolved** — The assessment is correct.
   - **Feedback** — The assessment is incorrect or the situation has changed. Explain why.

### For findings with no status annotation:

- Report as **Unresolved** — no response has been recorded yet.

## Step 3 — Verification Report

Output a verification report in the following format:

```
# Review Verification Report

**Review Document:** {path}
**Verification Date:** YYYY-MM-DD

## Verification Results

### Resolved

| # | Severity | Status | Verdict |
|---|----------|--------|---------|
| C-1 | Critical | Fixed | Fix is correct and complete. |
| M-2 | Major | Acknowledged | Rationale is sound. |

### Feedback Required

| # | Severity | Status | Issue |
|---|----------|--------|-------|
| M-1 | Major | Fixed | Fix addresses the null check but does not handle the error propagation path described in the finding. The `else` branch at line 85 still returns without logging. |
| m-1 | Minor | Fixed | Fix is correct, but introduced a new issue: the added `disconnect()` call at line 102 may fire during destruction, causing a double-disconnect with the existing cleanup in the destructor. |

### Unresolved

| # | Severity | Action | Notes |
|---|----------|--------|-------|
| m-3 | Minor | [Action Required] | No status annotation found. |

## Summary

- **Total findings verified:** N
- **Resolved:** N
- **Feedback required:** N
- **Unresolved (no status):** N
```

### Feedback Detail

For each finding that requires feedback, provide a detailed explanation after the summary table:

```
---

### M-1 — Feedback

**Original finding:** {brief description}
**Status claimed:** Fixed
**Actual state:** {what you observed in the code}
**Issue:** {what is wrong or incomplete}
**Suggestion:** {what should be done to fully resolve this}

---
```

## Step 4 — Write Feedback to Review Document

For each finding with a **Feedback** verdict, append a feedback annotation to the review document.

### Feedback Line Format

Insert the feedback line immediately after the corresponding status line (`> **{id} Status: Fixed**`, etc.):

```
> - **{finding-id} Feedback:** {Concise description of the issue and what should be done to fully resolve it.}
```

### Rules

- Insert the feedback line directly after the corresponding status line.
- Do not modify or delete existing status lines. Only append feedback lines.
- Do **not** add feedback lines for findings with a **Resolved** verdict.
- Do **not** add feedback lines for **Unresolved** findings (no status annotation — response has not been attempted yet).
- Write feedback descriptions in the same language as the review document.

### Example

Before write-back:

```markdown
## Major

| # | Location | Finding | Reviewer(s) | Action |
|---|----------|---------|-------------|--------|
| M-1 | `output.cpp:80` | Missing error check... | obs-sensei | **[Action Required]** Add return value check. |

> - **M-1 Status: Fixed** — Added `if (!output)` guard with `LOG_ERROR` before proceeding.

---
```

After write-back:

```markdown
## Major

| # | Location | Finding | Reviewer(s) | Action |
|---|----------|---------|-------------|--------|
| M-1 | `output.cpp:80` | Missing error check... | obs-sensei | **[Action Required]** Add return value check. |

> - **M-1 Status: Fixed** — Added `if (!output)` guard with `LOG_ERROR` before proceeding.
> - **M-1 Feedback:** Null check was added, but the `else` branch at line 85 returns without logging, leaving the error propagation path incomplete. Add `LOG_ERROR` to the `else` branch as well.

---
```

## Guidelines

- Be precise — cite specific file paths and line numbers when reporting issues.
- Do not speculate — only report issues you can confirm by reading the actual source code.
- A fix that resolves the original issue but introduces a new problem counts as **Feedback**, not **Resolved**.
- A partial fix that addresses some but not all aspects of the finding counts as **Feedback**.
- Do NOT modify source code. Changes to the review document are limited to appending feedback lines only.
