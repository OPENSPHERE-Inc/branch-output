---
name: review-resolve
description: Verify review finding resolutions against actual source code and provide feedback
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*)
---

# Review Verification

You are the **review verifier**. Your role is to re-read the updated review document, verify the resolution status of each finding against the actual source code, and report whether each finding has truly been resolved or whether further work is needed.

The verification result is written back by overwriting the review document (Step 4). The verification report is also emitted as a separate artifact (Step 3).

## Input

The user specifies a path to a review document (markdown). If the argument is `$ARGUMENTS`, interpret it as the path to the review document.

## Review Document Format

The review document is organized into sections by severity (`## Critical`, `## Major`, `## Minor`, `## Info`). Each finding is a `###` subsection with the following structure:

```markdown
### {finding-id} — `{location}`

- **Reviewers:** {reviewer name}

**Finding:**

{description}

> - **{id} Status: ...** — ...

---
```

Status lines appear as quoted lines between the "Finding" paragraph and the `---` delimiter. Because they are recorded **by appending, not by rewriting**, multiple lines may appear as the state evolves:

- `> - **{id} Status: Will Fix (assignee: {specialist})** — {triage rationale}` — Triaged as fixable. Fix not yet complete.
- `> - **{id} Status: Won't Fix** — {reason no action is needed}` — Triaged as no action needed (no further changes).
- `> - **{id} Status: Fixed** — {description of the fix}` — Fix completed. Typically preceded by a `Status: Will Fix` line.

The "current state" of a finding is determined by the **last line in the Status line group**:

| Trailing Status | State |
|-----------------|-------|
| None (no Status line at all) | Untriaged |
| `Will Fix` only | Triaged, fix not complete |
| `Will Fix` → `Fixed` | Fix complete |
| `Won't Fix` | Finalized as no action needed |

## Step 1 — Re-reading and Parsing

1. Read the entire review document.
2. Extract every finding from the **Critical**, **Major**, and **Minor** sections.
3. For each finding, extract:
   - The finding ID, severity, location (file and line number), and description
   - The Status line group (zero or more, possibly multiple) and the type of the trailing Status

## Step 2 — Verifying Each Finding

According to the type of the trailing Status, read the actual source code and verify the resolution status:

### Trailing line is `Status: Fixed` (preceded by `Status: Will Fix`):

1. Read the referenced files and lines.
2. Confirm that the described fix actually exists in the code.
3. Confirm that the fix fully addresses the original finding — not just partially.
4. Confirm that the fix does not introduce new problems (regressions, new bugs, style violations, thread-safety issues, resource leaks, etc.).
5. Verdict:
   - **Resolved** — The fix is accurate, complete, and introduces no new problems.
   - **Feedback** — The fix is missing, incomplete, or introduces new problems. Describe what remains.

### Trailing line is `Status: Won't Fix`:

1. Read the referenced files and understand the current state.
2. Evaluate whether the rationale for "won't fix" still holds in light of the current code.
3. Verdict:
   - **Resolved** — The rationale is sound and the decision is appropriate.
   - **Feedback** — The rationale is flawed or the situation has changed. Explain why.

### Trailing line is `Status: Will Fix` (no `Fixed` line follows):

- Report as **Unresolved** — Triaged but the fix is not yet complete.

### Findings with no Status line at all:

- Report as **Unresolved** — Triage has not yet been performed.

## Step 3 — Verification Report

Output the verification report in the following format **to the console** (do not write to file):

```
# Review Verification Report

**Review document:** {path}
**Verification date:** YYYY-MM-DD

## Verification Results

### Resolved

| # | Severity | Trailing Status | Verdict |
|---|----------|-----------------|---------|
| C-1 | Critical | Fixed | The fix is accurate and complete. |
| M-2 | Major | Won't Fix | The rationale is sound. |

### Feedback Required

| # | Severity | Trailing Status | Issue |
|---|----------|-----------------|-------|
| M-1 | Major | Fixed | The fix addresses the null check, but the error-propagation path described in the finding is not handled. The `else` branch on line 85 returns without logging. |
| m-1 | Minor | Fixed | The fix is correct, but introduces a new issue: the additional `disconnect()` call on line 102 fires on destruction, which can cause a double-disconnect together with the existing destructor cleanup. |

### Unresolved

| # | Severity | Trailing Status | Notes |
|---|----------|-----------------|-------|
| m-3 | Minor | (none) | No Status line recorded (untriaged). |
| m-4 | Minor | Will Fix | Triaged but no Fixed line recorded (fix not complete). |

## Summary

- **Findings verified:** N
- **Resolved:** N
- **Feedback required:** N
- **Unresolved:** N
```

### Feedback Detail

After the summary table, include detailed descriptions for each finding that requires feedback:

```
---

### M-1 — Feedback

**Original finding:** {concise description}
**Claimed status:** Fixed
**Actual state:** {what was observed in the code}
**Issue:** {what is incorrect or incomplete}
**Suggestion:** {what should be done to fully resolve it}

---
```

## Step 4 — Overwriting the Review Document

Reflect the verification results back into the review document. **Overwrite** the review document so it represents the latest integrated state of each finding's status and verification result.

### What to Update

For each finding, update according to the verification result. Preserve the entire Status line group; only **append**:

- **Resolved** — Append `✓ Verified` to **only the trailing Status line** (do not modify earlier lines such as `Will Fix`).
- **Feedback** — Append a Feedback line immediately after the trailing Status line (before the `---` delimiter).
- **Unresolved** — No changes (work is not yet started, or the fix is not yet complete).

Write descriptions (Feedback lines and the post-`✓ Verified` fix description) in the same language as the review document.

### Feedback Line Format

```
> - **{finding-id} Feedback:** {Concise description of the issue and what should be done to fully resolve it.}
```

### Verified Line Format (appending to the trailing Status)

```
> - **{finding-id} Status: Fixed ✓ Verified** — {description of the fix}
> - **{finding-id} Status: Won't Fix ✓ Verified** — {reason no action is needed}
```

### Examples (only the appended diff)

**Resolved (append `✓ Verified` to the trailing Status line):**

```diff
- > - **M-2 Status: Fixed** — Wrapped with OBSDataAutoRelease.
+ > - **M-2 Status: Fixed ✓ Verified** — Wrapped with OBSDataAutoRelease.
```

**Feedback (append a Feedback line immediately after the trailing Status line):**

```diff
  > - **M-1 Status: Will Fix (assignee: obs-sensei)** — Valid finding; should add return-value check.
  > - **M-1 Status: Fixed** — Added an `if (!output)` guard and `LOG_ERROR` before processing.
+ > - **M-1 Feedback:** The null check was added, but the `else` branch on line 85 returns without logging, leaving the error-propagation path incomplete. A `LOG_ERROR` should also be added in the `else` branch.
```

## Guidelines

- Be precise — when reporting an issue, cite the specific file path and line number.
- Do not speculate — only report issues that you have confirmed by reading the actual source code.
- Do not modify the source code. Changes to the review document are limited to appending `✓ Verified` or Feedback lines.
