---
description: Respond to review findings by triaging, fixing, self-reviewing, and updating the review document
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*), Bash(git add:*), Bash(git commit:*), Bash(git status:*), Bash(cmake:*), Bash(make:*), Bash(pwsh:*)
---

# Review Response

You are the **review response leader**. Your job is to process a review document, triage each finding, delegate fixes to the appropriate specialist agents, ensure quality via self-review, and update the review document with resolution status.

## Input

The user will specify a path to a review document (markdown). If the argument is `$ARGUMENTS`, interpret it as the path to the review document.

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--commit` | OFF | Create a git commit after each finding is fixed |

### `--commit` Option

When enabled, each finding's fix is committed to git after the fix is complete in Step 3 (Fix).

#### Commit Rules

- **Granularity**: Commit per finding whenever possible. Ideally, one finding's fix corresponds to one commit.
- **Multiple findings in the same file**: Even when fixing multiple findings in the same file sequentially, commit individually after each finding's fix is complete.
- **Commit message**: Describe the fix concisely. Do **not** include finding IDs (`C-1`, `M-1`, etc.) in commit messages.
- **Staging**: Stage only the files related to the fix (do not use `git add -A`).
- **Relationship to build verification**: Commits are made after Step 4 (Build Verification). If build errors occur, include their fixes before committing.

#### Commit Message Examples

```
fix: Add null check before accessing output pointer

fix: Guard audio buffer access with mutex lock

fix: Use OBSDataAutoRelease for RAII protection in lambda
```

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
4. Process **all** findings that are not yet resolved:
   - `**[Action Required]**` findings proceed to triage (Step 2) for fix decisions.
   - `**[No Action Needed]**` findings do not need fixes, but still require a status line with your assessment of whether the reviewer's rationale is sound (Step 4). This allows the reviewer to verify your judgment.

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

## Step 4 — Build Verification

After all fixes are complete, run a build to verify that the changes compile without errors.

1. Determine the appropriate build command for the current platform. Check for build scripts (`build.ps1`, `Makefile`, etc.) or use CMake presets as documented in `CLAUDE.md`.
2. Run the build. If the build fails:
   - Analyze the error output.
   - Delegate the fix to the appropriate specialist agent (same selection criteria as Step 3).
   - Re-run the build after the fix. Repeat until the build succeeds.
3. Report build results (success or the errors encountered and how they were resolved).

## Step 5 — Update the Review Document

After all fixes are complete, update the review document. Since the document uses markdown tables, status annotations are appended as blockquote lines **after the table, before the `---` section separator**.

### Status line format

For fixed findings:

```
> - **{finding-id} Status: Fixed** — {Brief description of the fix.}
```

For "Won't Fix" decisions (triaged by you during Step 2):

```
> - **{finding-id} Status: Won't Fix** — {Reason why no action is needed.}
```

For findings already marked `[No Action Needed]` by the reviewer — add your assessment of the reviewer's rationale:

```
> - **{finding-id} Status: Acknowledged** — {Your assessment: agree/disagree with the reviewer's rationale and why.}
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

## Major

| # | Location | Finding | Reviewer(s) | Action |
|---|----------|---------|-------------|--------|
| M-1 | `output.cpp:80` | Missing error check... | obs-sensei | **[Action Required]** Add return value check. |
| M-2 | `client.cpp:120` | Redundant copy... | cpp-sensei | **[No Action Needed]** Existing code, not introduced in this change. |

> **M-1 Status: Fixed** — Added `if (!output)` guard with `LOG_ERROR` before proceeding.
> **M-2 Status: Acknowledged** — Agree. This is pre-existing code outside the scope of this change. Tracked for future cleanup.

---
```

## Step 6 — Summary

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
| M-2 | Major | Acknowledged | — | Agree with reviewer rationale |
| m-1 | Minor | Won't Fix | — | Reason |
| ... | ... | ... | ... | ... |

## Statistics

- **Total findings processed:** N
- **Fixed:** N
- **Won't Fix:** N
- **Acknowledged ([No Action Needed]):** N
- **Already resolved (skipped):** N
```
