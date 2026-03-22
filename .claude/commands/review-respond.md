---
description: Respond to review findings by triaging, fixing, self-reviewing, and updating the review document
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash
---

# Review Response

You are the **review response leader**. Your job is to process a review document, triage each finding, delegate fixes to the appropriate specialist agents, ensure quality via self-review, and update the review document with resolution status.

## Input

The user will specify a path to a review document (markdown). If the argument is `$ARGUMENTS`, interpret it as the path to the review document.

## Review Document Format

The review document uses markdown tables grouped by severity. Each severity section has a table with these columns:

| Column | Description |
|--------|-------------|
| `#` | Finding ID (e.g., `C-1`, `M-1`, `m-1`) |
| Location | File path and line numbers |
| Description | Description of the issue |
| Reviewer | Reviewer name(s) |
| Action | Triage decision: `**[Action Required]**` or `**[No Action Needed]**` with rationale |

Severity sections: `## Critical`, `## Major`, `## Minor`, `## Info`.

The document may also contain:
- An actionable findings summary table at the end listing items that require fixes.
- A carry-over tracking table from prior review rounds.

## Step 1 — Parse the Review Document

1. Read the entire review document.
2. Extract all findings from the **Critical**, **Major**, and **Minor** sections (skip **Info**).
3. For each finding, extract:
   - Finding ID (e.g., `C-1`, `M-1`, `m-1`)
   - Severity (Critical / Major / Minor)
   - Location (file path and line numbers)
   - Description of the issue
   - Reviewer action (`[Action Required]` or `[No Action Needed]` with rationale)
   - Current resolution status (check if already marked as `**Status: Fixed**`)
4. Focus on findings marked `**[Action Required]**` that are not yet resolved. Findings marked `**[No Action Needed]**` are already triaged by the reviewer and do not need fixes.

## Step 2 — Triage

For each `[Action Required]` finding that is not yet resolved:

- **Will Fix** — The finding is valid and should be addressed.
- **Won't Fix** — The finding is not applicable, is a false positive, or the risk is acceptable. State the reason.
- **Needs Investigation** — Read the relevant source code to make a determination before deciding.

For "Needs Investigation" items, read the referenced files and make a final Will Fix / Won't Fix decision.

Present the triage results to the user and wait for confirmation before proceeding to fixes.

## Step 3 — Fix

For each "Will Fix" finding, delegate the fix to the most appropriate specialist agent. Select the specialist based on the nature of the finding:

| Specialist | When to use |
|------------|-------------|
| **cpp-sensei** | C++ language issues, memory management, RAII, thread safety, UB, performance |
| **qt-sensei** | Qt API usage, signal/slot, object lifetime, thread affinity, UI thread safety |
| **obs-sensei** | OBS API correctness, plugin lifecycle, source/filter/output, OBS threading model |
| **network-sensei** | Network programming, protocols, connection lifecycle, security |

### Agent Prompt Template

Each fix agent receives:

```
You are {specialist-name}, fixing review finding {finding-id}.

**Finding:** {description}
**Location:** {file_paths_and_lines}
**Reviewer action:** {action_column_content}

Instructions:
1. Read the relevant source file(s) to understand the current code and surrounding context.
2. Implement the fix. Follow the project's coding conventions (see CLAUDE.md).
3. After fixing, perform a self-review:
   - Re-read the modified code.
   - Verify the fix addresses the finding without introducing new issues.
   - Check for edge cases, thread safety, and resource leaks.
   - If the self-review reveals problems, fix them before reporting back.
4. Report what you changed and why, including any self-review corrections.
```

### Parallelization

- Group findings by file to avoid edit conflicts.
- Findings affecting **different files** may be fixed in parallel by launching multiple agents simultaneously.
- Findings affecting the **same file** must be fixed sequentially — launch them one at a time, waiting for each to complete before starting the next.

## Step 4 — Update the Review Document

After all fixes are complete, update the review document. Since the document uses markdown tables, status annotations are appended as blockquote lines **after the table, before the `---` section separator**.

### Status line format

For fixed findings:

```
> **{finding-id} Status: Fixed** — {Brief description of the fix.}
```

For "Won't Fix" decisions:

```
> **{finding-id} Status: Won't Fix** — {Reason why no action is needed.}
```

### Rules

- Each status line starts with the finding ID for traceability.
- Insert status blockquotes after the table in that section, before the `---` separator.
- Preserve all existing content — only add status lines.
- Update the actionable findings summary table if present: add a status column or append status notes.
- Write status descriptions in the same language as the review document.

### Example

Before:

```markdown
## Critical

| # | Location | Finding | Reviewer(s) | Action |
|---|----------|---------|-------------|--------|
| C-1 | `file.cpp:42` | Use-after-free risk... | cpp-sensei | **[Action Required]** Add ref counting. |

---
```

After:

```markdown
## Critical

| # | Location | Finding | Reviewer(s) | Action |
|---|----------|---------|-------------|--------|
| C-1 | `file.cpp:42` | Use-after-free risk... | cpp-sensei | **[Action Required]** Add ref counting. |

> **C-1 Status: Fixed** — Added `obs_data_addref()` and wrapped with `OBSDataAutoRelease` for RAII protection inside the lambda.

---
```

## Step 5 — Summary

Output a summary of all actions taken:

```
# Review Response Summary

**Review Document:** {path}
**Date:** YYYY-MM-DD

## Results

| # | Severity | Decision | Specialist | Notes |
|---|----------|----------|------------|-------|
| C-1 | Critical | Fixed | cpp-sensei | Brief description of fix |
| M-1 | Major | Fixed | cpp-sensei | Brief description of fix |
| m-1 | Minor | Won't Fix | — | Reason |
| ... | ... | ... | ... | ... |

## Statistics

- **Total findings processed:** N (actionable items marked [Action Required])
- **Fixed:** N
- **Won't Fix:** N
- **Already resolved (skipped):** N
```
