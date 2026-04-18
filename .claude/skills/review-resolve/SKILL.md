---
name: review-resolve
description: Verify review finding resolutions against actual source code and provide feedback
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*)
---

# Review Resolve

You are the **review verifier**. Your job is to reload an updated review document, verify each finding's resolution status against the actual source code, and report whether each finding is truly resolved or needs further work.

Verification results are written back by overwriting the review document (Step 4). A separate verification report is also output (Step 3).

## Input

The user will specify a path to a review document (markdown). If the argument is `$ARGUMENTS`, interpret it as the path to the review document.

## Review Document Format

The review document is organized into severity sections (`## Critical`, `## Major`, `## Minor`, `## Info`). Each finding is a `###` subsection with this structure:

```markdown
### {finding-id} — `{location}`

- **Reviewer(s):** {reviewer names}

**Finding:**

{description}

**Action:** **[Action Required]** or **[No Action Needed]** — {rationale}

---
```

Status annotations appear as blockquote lines between the Action line and the `---` separator:

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

Output a verification report **to the console** (do not write to a file) in the following format:

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

## Step 4 — Overwrite Review Document with Verification Results

Write verification results back to the review document by **overwriting** it. Update status annotations and add feedback for each finding based on verification.

### Update Actions

For each finding, update based on the verdict:

- **Resolved** — Append `✓ Verified` to the status line.
- **Feedback** — Keep the status line and add a feedback line immediately after it.
- **Unresolved** — No changes (response has not been attempted yet).

### Feedback Line Format

```
> **{finding-id} Feedback:** {Concise description of the issue and what should be done to fully resolve it.}
```

### Verified Line Format

```
> **{finding-id} Status: Fixed ✓ Verified** — {Description of the fix}
```

### Rules

- Overwrite the review document directly.
- Preserve the existing finding subsection structure (headings, metadata, Finding, Action, `---` separators).
- For **Resolved** findings, append `✓ Verified` to the existing status line.
- For **Feedback** findings, keep the status line and add a feedback line immediately after it (still before the `---` separator).
- Do **not** add feedback lines for **Resolved** or **Unresolved** findings.
- Write feedback descriptions in the same language as the review document.

### Example

Before update:

```markdown
## Major

### M-1 — `output.cpp:80`

- **Reviewer(s):** obs-sensei

**Finding:**

Missing error check on the return value of `obs_output_start()`.

**Action:** **[Action Required]** Add return value check.

> **M-1 Status: Fixed** — Added `if (!output)` guard with `LOG_ERROR` before proceeding.

---

### M-2 — `filter.cpp:120`

- **Reviewer(s):** cpp-sensei

**Finding:**

RAII not used for `obs_data_t` lifetime management.

**Action:** **[Action Required]** Use OBSDataAutoRelease.

> **M-2 Status: Fixed** — Wrapped with OBSDataAutoRelease.

---
```

After update:

```markdown
## Major

### M-1 — `output.cpp:80`

- **Reviewer(s):** obs-sensei

**Finding:**

Missing error check on the return value of `obs_output_start()`.

**Action:** **[Action Required]** Add return value check.

> **M-1 Status: Fixed** — Added `if (!output)` guard with `LOG_ERROR` before proceeding.
> **M-1 Feedback:** Null check was added, but the `else` branch at line 85 returns without logging, leaving the error propagation path incomplete. Add `LOG_ERROR` to the `else` branch as well.

---

### M-2 — `filter.cpp:120`

- **Reviewer(s):** cpp-sensei

**Finding:**

RAII not used for `obs_data_t` lifetime management.

**Action:** **[Action Required]** Use OBSDataAutoRelease.

> **M-2 Status: Fixed ✓ Verified** — Wrapped with OBSDataAutoRelease.

---
```

## Guidelines

- Be precise — cite specific file paths and line numbers when reporting issues.
- Do not speculate — only report issues you can confirm by reading the actual source code.
- A fix that resolves the original issue but introduces a new problem counts as **Feedback**, not **Resolved**.
- A partial fix that addresses some but not all aspects of the finding counts as **Feedback**.
- Do NOT modify source code. Review document changes are limited to status and feedback annotations.
