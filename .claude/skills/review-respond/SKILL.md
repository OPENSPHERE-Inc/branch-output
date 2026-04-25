---
name: review-respond
description: Respond to review findings by triaging, fixing, self-reviewing, and updating the review document
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*), Bash(git add:*), Bash(git commit:*), Bash(git status:*), Bash(cmake:*), Bash(make:*), Bash(pwsh:*), Bash(clang-format:*), Bash(cmake-format:*)
---

# Review Response

You are the **review response leader**. Your role is to process the review document, delegate primary triage to a separate sub-agent, delegate fixes to the appropriate specialist agents, ensure quality through self-review, and update the review document with the resolution status.

## Input

The user specifies a path to a review document (markdown). If the argument is `$ARGUMENTS`, interpret it as the path to the review document.

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--commit` | OFF | Create a commit per fixed finding |

### `--commit` Option

When enabled, commit the changes via git each time a fix for a finding is completed in Step 3 (Fix).

#### Commit Rules

- **Granularity**: Commit per finding whenever possible. The ideal is one commit per finding fix.
- **Multiple findings in the same file**: Even when sequentially fixing multiple findings that target the same file, commit individually as each finding's fix is completed.
- **Commit message**: Describe the fix concisely in 1–3 lines. **Do not include** finding IDs (`C-1`, `M-1`, etc.) in commit messages.
- **Staging**: Stage only the source code files relevant to the fix (do not use `git add -A`). **Do not commit the review document.**
- **Relation to build verification**: Commit after Step 4 (build verification). If a build error occurs, include its fix as well before committing.

#### Example Commit Message

```
fix: Add null check before accessing output pointer
```

## Review Document Format

The review document is organized into sections by severity (`## Critical`, `## Major`, `## Minor`, `## Info`). Each finding is a `###` subsection with the following structure:

**At input (before triage):**

The input review document is assumed to be **untriaged**. Findings have no Status lines yet; Step 2's triage finalizes `Will Fix` / `Won't Fix` and writes them in. After fixes are complete, an additional `Fixed` Status line is appended.

```markdown
### {finding-id} — `{location}`

- **Reviewers:** {reviewer name}

**Finding:**

{Description of the issue. May span multiple paragraphs. Code snippets are allowed.}

---
```

**After Step 2 triage (this skill writes these in):**

```markdown
### {finding-id} — `{location}`

- **Reviewers:** {reviewer name}

**Finding:**

{Description of the issue}

> - **{id} Status: Will Fix (assignee: {specialist})** — {triage rationale}
(or `> - **{id} Status: Won't Fix** — {reason no action is needed}`)

---
```

**After Step 5 fix completion (a Fixed line is appended for each Will Fix finding):**

```markdown
### {finding-id} — `{location}`

- **Reviewers:** {reviewer name}

**Finding:**

{Description of the issue}

> - **{id} Status: Will Fix (assignee: {specialist})** — {triage rationale}
> - **{id} Status: Fixed** — {concise description of the fix}

---
```

- Finding ID examples: `C-1` (Critical), `M-1` (Major), `m-1` (Minor), `I-1` (Info).
- Each finding is delimited by a trailing `---` horizontal rule.
- Status lines appear as quoted lines (`> ...`) between the "Finding" paragraph and the `---` delimiter. They are **appended, not rewritten**, so the state-transition history is preserved (e.g., `Will Fix` and `Fixed` appear on two consecutive lines).

The document may also contain carry-over tracking sections or summaries from previous rounds.

## Step 1 — Parsing the Review Document

1. Read the entire review document.
2. Extract every finding from the **Critical**, **Major**, and **Minor** sections (skip **Info**).
3. For each finding, extract:
   - The finding ID (e.g., `C-1`, `M-1`, `m-1`)
   - The severity (Critical / Major / Minor)
   - The location (file path and line number)
   - The description of the issue
   - The list of current Status lines (`> - **{id} Status: ...**`) — zero or more, possibly multiple in sequence
4. Classify based on the state of the Status lines:
   - **No Status line** → Target of Step 2 (Triage)
   - **Only `Status: Will Fix` (no Fixed line)** → Triaged, fix not yet complete. Target of Step 3 (Fix)
   - **Has `Status: Won't Fix`** → Finalized, skip
   - **Has `Status: Fixed`** → Already fixed, skip

## Step 2 — Triage (Delegated to a Separate Sub-Agent)

Triage is delegated by launching a **separate single sub-agent, distinct from the specialist agents that perform fixes**. Separating its context from the fix-execution agents ensures the triage decision is not biased by fix-time considerations.

This triage sub-agent is responsible not only for the decision but also for **writing the decision back into the review document** (see "3. Reflecting in the review document" in the sub-agent prompt below for the exact format).

**Launch procedure:**

1. Launch a new sub-agent via the Agent tool. Example prompt:

```
You are responsible for the primary triage of the review document and for reflecting the result back into the document.
**This task does not modify source code.** Only edit the review document and report the triage results.

Review document: {document_path}

Tasks:

1. **Triage** — For each Critical / Major / Minor finding without a Status line, decide one of the following:
   - **Will Fix** — Valid finding, should be addressed
   - **Won't Fix** — Not applicable / false positive / risk acceptable (record the reason)
   - **Needs Investigation** — Read the relevant source to decide → ultimately resolve to Will Fix / Won't Fix

   **Won't Fix decision guidelines** — Mark a finding as Won't Fix if any of the following apply:
   1. The finding is out of scope of the branch diff.
   2. The finding targets an existing-code bug not introduced by this branch.
   3. The finding rests on a wrong premise or is technically incorrect.
   4. The finding is something that, given the project's purpose, intended use, and expected audience, can reasonably be inferred to be tolerated.
   5. A purely preference-based refactoring (no concrete justification in terms of correctness, safety, performance, or maintainability).

   **High-severity exception:** Even when the verdict is Won't Fix, if the severity is **Critical** or **Major**, explicitly note in the rationale that the recommended path is to address it in a separate PR (e.g., "Won't Fix — existing-code bug. Recommend addressing in a separate PR").

2. **Specialist assignment (Will Fix only)** — Assign the most suitable specialist for each Will Fix finding:
   - cpp-sensei / qt-sensei / obs-sensei / network-sensei / av-sensei / devops-sensei / python-sensei / lua-sensei

3. **Reflecting in the review document** — Preserving the existing structure, append the following Status line as a quoted line between each finding's "Finding" paragraph and its `---` delimiter:
   - **Will Fix:** `> - **{id} Status: Will Fix (assignee: {specialist})** — {triage rationale}`
   - **Won't Fix:** `> - **{id} Status: Won't Fix** — {reason no action is needed}`

4. **Reporting format:**
   - Triage result table:
     | Finding ID | Verdict | Assigned Specialist | Rationale |
     |------------|---------|---------------------|-----------|
     | C-1 | Will Fix | cpp-sensei | ... |
     | M-2 | Won't Fix | — | Existing code, out of scope for this round |
   - Whether any Will Fix findings exist, and the count.
```

2. Receive the result from the sub-agent.
3. Present the triage result to the user and wait for confirmation before proceeding to fixes.

## Step 3 — Fix

For each finding decided as "will fix", delegate the fix to the most appropriate specialist agent. Choose the specialist based on the nature of the finding:

| Specialist | When to use |
|------------|-------------|
| **cpp-sensei** | C++ language issues, memory management, RAII, thread safety, undefined behavior, performance |
| **qt-sensei** | Qt API usage, signals/slots, object lifetime, thread affinity, UI thread safety |
| **obs-sensei** | OBS API correctness, plugin lifecycle, source/filter/output, OBS threading model |
| **network-sensei** | Networking, protocols, connection lifecycle, security |
| **av-sensei** | Video/audio/streaming quality, encoder settings, media processing |
| **devops-sensei** | CI/CD, CMake, build scripts, formatters, packaging |
| **python-sensei** | Python scripts, OBS Python Script |
| **lua-sensei** | Lua scripts, OBS Lua Script |

### Agent Prompt Template

Pass the following to each fix agent:

```
You are {specialist-name}. Fix review finding {finding-id}.

**Finding:** {description}
**Location:** {file_paths_and_lines}
**Triage verdict:** Will Fix — {triage_reason}

Procedure:
1. Read the relevant source files and understand the current code and surrounding context.
2. Implement the fix. Follow the project's coding conventions (see CLAUDE.md).
3. After the fix, perform a self-review:
   - Re-read the changed code.
   - Verify that the fix addresses the finding and does not introduce new issues.
   - Check edge cases, thread safety, and resource leaks.
   - If the self-review uncovers any problems, fix them before reporting.
4. Report what you changed and why (including any self-review fixes).

For comments, follow the rules in `.claude/rules/comment-discipline.md` (auto-loaded).
```

### Parallelization

- Group findings by file to avoid edit conflicts.
- Findings affecting **different files** can be fixed in parallel by launching multiple agents simultaneously.
- Findings affecting the **same file** must be fixed sequentially — launch them one at a time, waiting for each to complete before starting the next.

## Step 4 — Format Verification & Build Verification

After all fixes are complete, run code-format verification and a build to validate the quality of the changes.

### 4a. Format Verification

1. Identify the source files (`.cpp`, `.hpp`, `.h`, `.c`) modified in Step 3.
2. Run `clang-format` on each file to check for format violations:
   ```bash
   clang-format -style=file -fallback-style=none --dry-run -Werror <file>
   ```
3. If there are format violations:
   - Auto-fix with `clang-format -i -style=file -fallback-style=none <file>`.
   - Inspect the resulting diff and verify it does not include unintended changes.
4. If CMake files (`CMakeLists.txt`, `*.cmake`) were modified, verify and fix them similarly with `cmake-format`.

### 4b. Build Verification

1. Identify the build command appropriate for the current platform. Check the build script (`build.ps1`, `Makefile`, etc.) or use the CMake preset documented in `CLAUDE.md`.
2. Run the build. If the build fails:
   - Analyze the error output.
   - Delegate the fix to the appropriate specialist agent (using the same selection criteria as Step 3).
   - Re-run the build after the fix. Repeat until it succeeds.
3. Report the build result (success, or the errors encountered and how they were resolved).

## Step 5 — Updating the Review Document

For each finding fixed as Will Fix, leave the existing `Status: Will Fix` line in place and immediately after it append the following Status line (see "Review Document Format" for the rationale of this format):

```
> - **{finding-id} Status: Fixed** — {Concise description of the fix.}
```

Won't Fix Status lines were already written by the Step 2 triage sub-agent, so they are not appended again here. Write the description in the same language as the review document.

## Step 6 — Summary

Output a summary of all actions taken:

```
# Review Response Summary

**Review document:** {path}
**Date:** YYYY-MM-DD

## Results

| # | Severity | Verdict | Specialist | Notes |
|---|----------|---------|------------|-------|
| C-1 | Critical | Fixed | cpp-sensei | Concise description of fix |
| M-1 | Major | Fixed | cpp-sensei | Concise description of fix |
| M-2 | Major | Won't Fix | — | Existing code, out of scope for this round |
| m-1 | Minor | Won't Fix | — | Reason |
| ... | ... | ... | ... | ... |

## Statistics

- **Findings processed:** N
- **Fixed:** N
- **Won't Fix:** N
- **Resolved (skipped, existing Status line present):** N
```
