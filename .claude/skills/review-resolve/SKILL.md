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

The review document is organized into severity-based sections (`## Critical`, `## Major`, `## Minor`, `## Info`). Each finding is a `###` subsection, with quoted judgment lines (`> ...`) appended between the "Finding" paragraph and the `---` delimiter as the finding moves through the workflow:

```markdown
### {finding-id} — `{location}`

- **Reviewers:** {reviewer name}

**Finding:**

{description}

> - **{id} Triage: ...** — ...
> - **{id} Estimate ({specialist}): ...** — ...
> - **{id} Status: ...** — ...

---
```

Judgment lines are recorded **by appending, not by rewriting**, so multiple lines may appear side by side as the state evolves:

| Prefix | Verdict | Meaning |
|--------|---------|---------|
| `Triage:` | `🔧 Will Fix` | Confirmed for fixing during triage |
| 〃 | `🚫 Won't Fix` | Confirmed as no action needed during triage |
| `Estimate ({specialist}):` | `▶️ Maintain` | Estimate keeps the Will Fix verdict; proceed with the fix |
| 〃 | `🔻 Downgrade` | Estimate overturns the triage verdict and does not fix (no alternative) |
| 〃 | `🚧 Alternative` | Estimate overturns the triage verdict but addresses it via an alternative such as a FIXME |
| `Status:` | `🟢 Fixed` | Fix complete (a Maintain fix or an Alternative FIXME addition) |

The "current state" of a finding is determined by the **last judgment line that was appended**:

| Last judgment line | State |
|---|---|
| None | Untriaged |
| `Triage: 🔧 Will Fix` | Triaged, estimate not complete |
| `Triage: 🚫 Won't Fix` | Finalized as no action needed |
| `Estimate: ▶️ Maintain` | Estimated, fix not complete |
| `Estimate: 🔻 Downgrade` | Estimate flipped it to no action |
| `Estimate: 🚧 Alternative` | Estimate flipped it to alternative; FIXME addition not complete |
| `Status: 🟢 Fixed` | Fix complete (Maintain fix, or Alternative FIXME addition) |

## Step 1 — Re-reading and Parsing

1. Read the entire review document.
2. Extract every finding from the **Critical**, **Major**, and **Minor** sections.
3. For each finding, extract:
   - The finding ID, severity, location (file and line number), and description
   - The judgment line group (zero or more, with Triage / Estimate / Status possibly appearing in sequence) and the last judgment line

## Step 2 — Verifying Each Finding

According to the last judgment line, read the actual source code and verify the resolution status:

### Last line is `Status: 🟢 Fixed`:

The expected fix content depends on whether the immediately preceding Estimate line is `Maintain` or `Alternative`.

1. Read the referenced files and lines.
2. Confirm that the described fix actually exists in the code.
   - **If the preceding line is `Estimate: ▶️ Maintain`:** Confirm that a regular fix addressing the finding (including logic changes) has been applied.
   - **If the preceding line is `Estimate: 🚧 Alternative`:** Confirm that an appropriate `FIXME:` / `TODO:` comment has been added at the relevant site. Also check that the comment wording is roughly aligned with the FIXME direction recorded in the Estimate line. Do not expect logic changes.
3. Confirm that the fix fully addresses the original finding (Maintain case), or that the FIXME comment carries enough information for a future fix (Alternative case).
4. Confirm that the fix does not introduce new problems (regressions, new bugs, style violations, thread-safety issues, resource leaks, etc.).
5. Verdict:
   - **Resolved** — The fix is accurate, complete, and introduces no new problems.
   - **Feedback** — The fix is missing, incomplete, or introduces new problems. Describe what remains.

### Last line is `Triage: 🚫 Won't Fix`:

1. Read the referenced files and understand the current state.
2. Evaluate whether the "won't fix" rationale still holds in light of the current code.
3. Verdict:
   - **Resolved** — The rationale is sound and the decision is appropriate.
   - **Feedback** — The rationale is flawed or the situation has changed. Explain why.

### Last line is `Estimate: 🔻 Downgrade`:

1. Read the referenced files and understand the current state.
2. Evaluate whether the downgrade rationale on the Estimate line (spread signals, Cost, Future, judgment reason) still holds in light of the current code.
3. If "recommend separate PR" is included in the rationale, confirm that the call is appropriate (be especially careful when a Critical / Major finding is downgraded without a separate-PR recommendation).
4. Verdict:
   - **Resolved** — The downgrade rationale is sound.
   - **Feedback** — The downgrade rationale is flawed or the situation has changed. Explain why.

### Last line is `Estimate: 🚧 Alternative`:

- Report as **Unresolved** — The estimate scheduled an alternative response (FIXME addition), but the FIXME comment has not yet been added.

### Last line is `Estimate: ▶️ Maintain`:

- Report as **Unresolved** — The estimate decided to Maintain, but the fix is not yet complete.

### Last line is `Triage: 🔧 Will Fix`:

- Report as **Unresolved** — Triaged but the estimate is not yet complete.

### Findings with no judgment line at all:

- Report as **Unresolved** — Triage has not yet been performed either.

## Step 3 — Verification Report

Output the verification report in the following format **to the console** (do not write to file):

```
# Review Verification Report

**Review document:** {path}
**Verification date:** YYYY-MM-DD

## Verification Results

### Resolved

| # | Severity | Last judgment line | Verdict |
|---|----------|--------------------|---------|
| C-1 | Critical | Status: 🟢 Fixed | The fix is accurate and complete. |
| M-2 | Major | Triage: 🚫 Won't Fix | The rationale is sound. |
| M-3 | Major | Estimate: 🔻 Downgrade | The downgrade rationale is sound; separate-PR recommendation is recorded. |
| M-4 | Major | Status: 🟢 Fixed | Alternative FIXME has been added; wording matches the Estimate direction. |

### Feedback Required

| # | Severity | Last judgment line | Issue |
|---|----------|--------------------|-------|
| M-1 | Major | Status: 🟢 Fixed | The fix addresses the null check, but the error-propagation path described in the finding is not handled. The `else` branch on line 85 returns without logging. |
| m-1 | Minor | Status: 🟢 Fixed | The fix is correct, but introduces a new issue: the additional `disconnect()` call on line 102 fires on destruction, which can cause a double-disconnect together with the existing destructor cleanup. |
| M-5 | Major | Estimate: 🔻 Downgrade | The downgrade rationale is "FIXME-originated" only, but no existing FIXME can be found at the site, so the rationale does not match the current code. |

### Unresolved

| # | Severity | Last judgment line | Notes |
|---|----------|--------------------|-------|
| m-3 | Minor | (none) | No judgment line recorded (untriaged). |
| m-4 | Minor | Triage: 🔧 Will Fix | Triaged but the estimate is not complete. |
| m-5 | Minor | Estimate: ▶️ Maintain | Estimated but the fix is not complete. |
| m-6 | Minor | Estimate: 🚧 Alternative | FIXME addition is not complete. |

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
**Last judgment line:** Status: 🟢 Fixed
**Actual state:** {what was observed in the code}
**Issue:** {what is incorrect or incomplete}
**Suggestion:** {what should be done to fully resolve it}

---
```

## Step 4 — Overwriting the Review Document

Reflect the verification results back into the review document. **Overwrite** the review document so it represents the latest integrated state of each finding's status and verification result.

### What to Update

For each finding, update according to the verification result. Preserve every judgment line; only **append**:

- **Resolved** — Append `✅ Verified` to **only the last judgment line** (do not modify earlier Triage or Estimate lines).
- **Feedback** — Append a Feedback line immediately after the last judgment line (before the `---` delimiter).
- **Unresolved** — No changes (the work is not yet started, or the estimate / fix is not yet complete).

Write descriptions (Feedback lines, and the text after `✅ Verified`) in the same language as the review document.

### Feedback Line Format

```
> - **{finding-id} Feedback:** {Concise description of the issue and what should be done to fully resolve it.}
```

### Verified Line Format (appending to the last judgment line)

```
> - **{finding-id} Status: 🟢 Fixed ✅ Verified** — {description of the fix}
> - **{finding-id} Triage: 🚫 Won't Fix ✅ Verified** — {reason no action is needed}
> - **{finding-id} Estimate ({specialist}): 🔻 Downgrade ✅ Verified** — Cost: ..., Future: ..., Signals: ... — {downgrade rationale}
```

### Examples (only the appended diff)

**Resolved (append `✅ Verified` to the last judgment line):**

```diff
- > - **M-2 Status: 🟢 Fixed** — Wrapped with OBSDataAutoRelease.
+ > - **M-2 Status: 🟢 Fixed ✅ Verified** — Wrapped with OBSDataAutoRelease.
```

**Feedback (append a Feedback line immediately after the last judgment line):**

```diff
  > - **M-1 Triage: 🔧 Will Fix (assignee: obs-sensei)** — Valid finding; should add return-value check.
  > - **M-1 Estimate (obs-sensei): ▶️ Maintain** — Cost: S, Future: S, Signals: none
  > - **M-1 Status: 🟢 Fixed** — Added an `if (!output)` guard and `LOG_ERROR` before processing.
+ > - **M-1 Feedback:** The null check was added, but the `else` branch on line 85 returns without logging, leaving the error-propagation path incomplete. A `LOG_ERROR` should also be added in the `else` branch.
```

## Guidelines

- Be precise — when reporting an issue, cite the specific file path and line number.
- Do not speculate — only report issues that you have confirmed by reading the actual source code.
- Do not modify the source code. Changes to the review document are limited to appending `✅ Verified` or Feedback lines.
