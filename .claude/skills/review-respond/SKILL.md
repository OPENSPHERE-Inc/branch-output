---
name: review-respond
description: Triage, estimate, fix, self-review, and update the review document for review findings
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*), Bash(git add:*), Bash(git commit:*), Bash(git status:*), Bash(cmake:*), Bash(make:*), Bash(pwsh:*), Bash(clang-format:*), Bash(cmake-format:*)
---

# Review Response

You are the **review response leader**. Your role is to process the review document, delegate primary triage to a separate sub-agent, have specialist sub-agents estimate the cost of Will Fix findings, delegate fixes to the appropriate specialist agents, ensure quality through self-review, and update the review document with the resolution status.

## Input

The user specifies a path to a review document (markdown). If the argument is `$ARGUMENTS`, interpret it as the path to the review document.

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--commit` | OFF | Create a commit per fixed finding |
| `--confirm` | OFF | Wait for user confirmation immediately after estimates are gathered |

### `--commit` Option

When enabled, commit the changes via git each time a fix for a finding is completed in Step 4 (Fix). Estimates (Step 3) involve no source code changes and are not subject to commits.

#### Commit Rules

- **Granularity**: Commit per finding whenever possible. The ideal is one commit per finding fix.
- **Multiple findings in the same file**: Even when sequentially fixing multiple findings that target the same file, commit individually as each finding's fix is completed.
- **Commit message**: Describe the fix concisely in 1–3 lines. **Do not include** finding IDs (`C-1`, `M-1`, etc.) in commit messages.
- **Staging**: Stage only the source code files relevant to the fix (do not use `git add -A`). **Do not commit the review document.**
- **Relation to build verification**: Commit after Step 5 (build verification). If a build error occurs, include its fix as well before committing.

#### Example Commit Message

```
fix: Add null check before accessing output pointer
```

### `--confirm` Option

When enabled, wait for user confirmation immediately after the Step 3 (Estimate) result table is displayed on the console. Do not confirm right after triage. Reason: triage alone does not surface spread risk and gives the user too little to weigh in on. Only once the estimates (cost / signals / downgrade proposals) are in does the user have meaningful grounds for intervention, so the confirmation point is consolidated into a single pass.

## Review Document Format

The review document is organized into severity-based sections (`## Critical`, `## Major`, `## Minor`, `## Info`). Each finding is a `###` subsection, and as it moves through the workflow, quoted verdict lines (`> ...`) accumulate between the "Finding" paragraph and the `---` delimiter. Verdict lines are **appended, not rewritten**, so the state-transition history is preserved.

### Line Types and Verdict Meanings

Verdict lines are classified by three prefixes. At each stage exactly one prefix is appended (no mixing). Each verdict value carries an emoji **placed immediately before the verdict value** for visual identification (e.g., `Triage: 🔧 Will Fix`):

| Prefix | Stage | Verdict | Emoji | Meaning |
|--------|-------|---------|-------|---------|
| `Triage:` | Step 2 (Triage) | `Will Fix` | 🔧 | Confirmed for fixing |
| `Triage:` | 〃 | `Won't Fix` | 🚫 | Confirmed as no action needed |
| `Estimate ({specialist}):` | Step 3 (Estimate) | `Maintain` | ▶️ | Keep the triage verdict (proceed with fix) |
| `Estimate ({specialist}):` | 〃 | `Downgrade` | 🔻 | Overturn the triage verdict and do not fix (no alternative; "recommend separate PR" may be noted in the rationale) |
| `Estimate ({specialist}):` | 〃 | `Alternative` | 🚧 | Overturn the triage verdict but address it via an alternative such as adding a FIXME comment ("recommend separate PR" may be noted in the rationale) |
| `Status:` | Step 6 (Fix complete) | `Fixed` | 🟢 | Fix complete |

After verification (review-resolve) a trailing `✅ Verified` line is appended.

### Line Format

```
> - **{id} Triage: 🔧 Will Fix (assignee: {specialist})** — {triage rationale}
> - **{id} Triage: 🚫 Won't Fix** — {reason no action is needed}

> - **{id} Estimate ({specialist}): ▶️ Maintain** — Cost: {S/M/L}, Future: {S/M/L}, Signals: {none | the applicable subset of a,b,c,d,e}
> - **{id} Estimate ({specialist}): 🔻 Downgrade** — Cost: ..., Future: ..., Signals: ... — {downgrade rationale (include "recommend separate PR" if applicable)}
> - **{id} Estimate ({specialist}): 🚧 Alternative** — Cost: ..., Future: ..., Signals: ... — Add FIXME: {direction of the FIXME} (include "recommend separate PR" if applicable)

> - **{id} Status: 🟢 Fixed** — {concise description of the fix}
```

Estimate lines must be **complete on a single line**. Maintain carries only the quantitative fields and does not repeat the already-stated triage rationale. Downgrade and Alternative append the rationale (or FIXME direction) at the end.

### At Input (Before Triage)

The input review document is assumed to be **untriaged**. No Triage, Estimate, or Status line has been added yet.

```markdown
### {finding-id} — `{location}`

- **Reviewers:** {reviewer name}

**Finding:**

{Description of the issue. May span multiple paragraphs. Code snippets are allowed.}

---
```

### After Step 2 (Triage)

```markdown
### {finding-id} — `{location}`

- **Reviewers:** {reviewer name}

**Finding:**

{Description of the issue}

> - **{id} Triage: 🔧 Will Fix (assignee: {specialist})** — {triage rationale}

---
```

Or a `Triage: 🚫 Won't Fix` line.

### After Step 3 (Estimate, Will Fix only)

Maintain example:

```markdown
> - **{id} Triage: 🔧 Will Fix (assignee: {specialist})** — {triage rationale}
> - **{id} Estimate ({specialist}): ▶️ Maintain** — Cost: M, Future: S, Signals: b,d
```

Downgrade example:

```markdown
> - **{id} Triage: 🔧 Will Fix (assignee: {specialist})** — {triage rationale}
> - **{id} Estimate ({specialist}): 🔻 Downgrade** — Cost: L, Future: M, Signals: a,b,c — {downgrade rationale; include "recommend separate PR" when applicable}
```

Alternative example:

```markdown
> - **{id} Triage: 🔧 Will Fix (assignee: {specialist})** — {triage rationale}
> - **{id} Estimate ({specialist}): 🚧 Alternative** — Cost: L, Future: M, Signals: a,b — Add FIXME: {direction of the FIXME; include "recommend separate PR" when applicable}
```

### After Step 6 (Fix Complete)

Maintain fix:

```markdown
> - **{id} Triage: 🔧 Will Fix (assignee: {specialist})** — {triage rationale}
> - **{id} Estimate ({specialist}): ▶️ Maintain** — Cost: ..., Future: ..., Signals: ...
> - **{id} Status: 🟢 Fixed** — {concise description of the fix}
```

Alternative FIXME insertion:

```markdown
> - **{id} Triage: 🔧 Will Fix (assignee: {specialist})** — {triage rationale}
> - **{id} Estimate ({specialist}): 🚧 Alternative** — Cost: ..., Future: ..., Signals: ... — Add FIXME: ...
> - **{id} Status: 🟢 Fixed** — Added FIXME comment at {file:line}
```

### Notes

- Finding ID examples: `C-1` (Critical), `M-1` (Major), `m-1` (Minor), `I-1` (Info).
- Each finding is delimited by a trailing `---` horizontal rule.
- Each verdict line is a quoted line (`> ...`) sitting between the "Finding" paragraph and the `---` delimiter.
- The document may also contain carry-over tracking sections or summaries from previous rounds.

## Step 1 — Parsing the Review Document

1. Read the entire review document.
2. Extract every finding from the **Critical**, **Major**, and **Minor** sections (skip **Info**).
3. For each finding, extract:
   - The finding ID (e.g., `C-1`, `M-1`, `m-1`)
   - The severity (Critical / Major / Minor)
   - The location (file path and line number)
   - The description of the issue
   - The list of current verdict lines (Triage / Estimate / Status) — zero or more
4. Classify based on the trailing verdict line:
   - **No verdict line** → Target of Step 2 (Triage)
   - **Trailing line is `Triage: 🔧 Will Fix`** → Target of Step 3 (Estimate)
   - **Trailing line is `Estimate: ▶️ Maintain`** → Target of Step 4 (Fix)
   - **Trailing line is `Estimate: 🚧 Alternative`** → Target of Step 4 (FIXME insertion)
   - **Trailing line is `Triage: 🚫 Won't Fix`** → Finalized as no action; skip
   - **Trailing line is `Estimate: 🔻 Downgrade`** → Estimate decided no fix; skip
   - **Trailing line is `Status: 🟢 Fixed`** → Already fixed; skip

## Step 2 — Triage (Delegated to a Separate Sub-Agent)

Triage is delegated by launching a **single sub-agent that is separate from the specialist agents performing the fixes**. Separating its context from the fix-execution agents ensures the triage decision is not biased by fix-time considerations.

This triage sub-agent is responsible not only for the decision but also for **writing the verdict back into the review document**.

**Launch procedure:**

1. Launch a new sub-agent via the Agent tool. Example prompt:

```
You are responsible for the primary triage of the review document and for reflecting the result back into the document.
**This task does not modify source code.** Only edit the review document and report the triage results.

Review document: {document_path}

Tasks:

1. **Triage** — For each Critical / Major / Minor finding without a Triage line, decide one of the following:
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

3. **Reflecting in the review document** — Preserving the existing structure, append the following Triage line as a quoted line between each finding's "Finding" paragraph and its `---` delimiter. **Place the emoji immediately before the verdict value.**
   - **Will Fix:** `> - **{id} Triage: 🔧 Will Fix (assignee: {specialist})** — {triage rationale}`
   - **Won't Fix:** `> - **{id} Triage: 🚫 Won't Fix** — {reason no action is needed}`

4. **Reporting format:**
   - Triage result table:
     | Finding ID | Verdict | Assigned Specialist | Rationale |
     |------------|---------|---------------------|-----------|
     | C-1 | 🔧 Will Fix | cpp-sensei | ... |
     | M-2 | 🚫 Won't Fix | — | Existing code, out of scope for this round |
   - Whether any Will Fix findings exist, and the count.
```

2. Receive the result from the sub-agent.
3. Present the triage result to the user (user confirmation prior to fixing happens after the Step 3 estimates; do not confirm immediately after triage).

## Step 3 — Estimate (Delegated in Parallel to Specialist Sub-Agents)

For each finding decided as Will Fix in triage, launch a sub-agent of the **same specialist that will actually perform the fix** in parallel, have it estimate the fix cost, and have it decide whether to keep or overturn the verdict.

**Purpose of the estimate phase:**

- **Cost estimate** — Quantify the work required for the fix from the specialist's perspective.
- **Future-cost projection** — Predict whether this fix will pull in further fixes in future rounds or separate PRs.
- **Verdict re-examination** — When the cost is disproportionate to the value of the finding, overturn the triage verdict (Downgrade) or switch to an alternative approach (Alternative).

**Verdict options:**

- **▶️ Maintain** — Keep the triage verdict (proceed with the fix). The cost is reasonable; move forward with the fix.
- **🔻 Downgrade** — Overturn the triage verdict and do not fix. No alternative is taken ("recommend separate PR" may be noted in the rationale).
- **🚧 Alternative** — Overturn the triage verdict but address it lightweightedly via an alternative such as a FIXME comment ("recommend separate PR" may be noted in the rationale).

**Spread signals** (the criteria the estimate agent uses to judge applicability):

- **a. Introduction of a new concept** — The fix requires bringing in a library function / API / language feature not previously used in the codebase.
- **b. Expansion of fix scope** — The fix drags in files / modules / layers that were not modified on the current branch.
- **c. Asynchronous-execution timing interference** — Effects on UI thread blocking, callback execution order, signal/slot connection types, etc.
- **d. Future cost** — Stop-gap measures left in place, FIXMEs deferred, missing abstractions, etc., that invite future rounds or separate PRs.
- **e. Will Fix originating from a FIXME** — A finding that was originally left intentionally as a `FIXME:` / `TODO:` in the code or in a previous round, or one where the reviewer themselves proposed "make it a FIXME", was triaged as Will Fix.

**Launch procedure:**

1. For each Will Fix finding, launch a specialist sub-agent **in parallel** via the Agent tool (all findings can be parallelized; since estimates do not edit source code, multiple findings against the same file may also be launched in parallel). Example prompt:

```
You are {specialist-name}. You are responsible for estimating the fix cost of review finding {finding-id}.
**This task modifies neither source code nor the review document.** Investigation and verdict only.

**Finding:** {description}
**Location:** {file_paths_and_lines}
**Triage verdict:** Will Fix — {triage_reason}

Tasks:

1. Read the relevant source and mentally assemble the changes the fix requires.
2. For each of the following spread signals, decide whether it applies:
   a. Introduction of a new concept (a previously unused library function / API / language feature)
   b. Expansion of fix scope (files / modules not modified on the current branch)
   c. Asynchronous-execution timing interference (UI thread blocking, callback ordering, Qt connection-type changes, etc.)
   d. Future cost (stop-gap measures / deferred FIXMEs / missing abstractions)
   e. Will Fix originating from a FIXME (originally left intentionally as a FIXME/TODO, or a "make it a FIXME" proposal that triage flipped to Will Fix)
3. Compute Cost (S/M/L) and Future cost (S/M/L). The granularity may be coarse, but state the basis in 1–2 sentences.
4. Verdict (Downgrade / Alternative may be chosen regardless of severity):
   - **Maintain** — Cost is reasonable, proceed with the fix.
   - **Downgrade** — Overturn the triage verdict; do not fix. No alternative. Include "recommend separate PR" in the rationale where appropriate.
   - **Alternative** — Overturn the triage verdict but address it via an alternative such as a FIXME comment. Concisely state the direction of the FIXME wording. Include "recommend separate PR" where appropriate.

   The higher the severity, the more strictly the rationale for choosing Downgrade (no alternative) is scrutinized. Critical / Major findings should typically prefer Alternative (add FIXME) or "Downgrade + recommend separate PR". Minor / Info findings tolerate Downgrade more readily.

Reporting format:

- Verdict: Maintain / Downgrade / Alternative
- Cost: S/M/L, Future: S/M/L, applicable signals: a/b/c/d/e (multiple allowed; none also allowed)
- Maintain: Do not repeat the triage rationale. Concisely state the basis for the signal selection only.
- Downgrade: Downgrade rationale (2–4 sentences; include "recommend separate PR" if applicable).
- Alternative: Direction of the FIXME (a summary of the problem to be recorded in the comment plus the recommended fix direction; include "recommend separate PR" if applicable).

Do not edit source code or the review document under any circumstances.
```

2. Receive the results from every estimate agent.

3. **Reflecting in the review document** (performed by the leader yourself) — Append a single Estimate line immediately after the `Triage: 🔧 Will Fix` line of each Will Fix finding. Place the emoji (▶️ / 🔻 / 🚧) immediately before the verdict value. See the "Review Document Format" section for the exact format.

4. **Display the estimate result table on the console:**

   | Finding ID | Specialist | Cost | Future | Signals | Verdict | Notes |
   |------------|------------|------|--------|---------|---------|-------|
   | C-1 | cpp-sensei | M | S | b,d | ▶️ Maintain | — |
   | M-2 | qt-sensei | L | M | a,b,c | 🔻 Downgrade | Recommend separate PR |
   | M-3 | obs-sensei | L | M | a,b | 🚧 Alternative | Add FIXME + recommend separate PR |

5. If `--confirm` is enabled, wait for user confirmation before proceeding to fixes.

**When both Maintain and Alternative are zero (all Downgrade):** Skip Steps 4 and 5 and proceed to Step 6 (document update). Estimate lines have already been written in this step, so no additional changes are needed in Step 6. Step 7 (summary) records them as Downgrade.

## Step 4 — Fix

This step covers two kinds of work, both delegated to the most appropriate specialist agent:

- **Regular fix** — Fix for a finding judged as `Estimate: Maintain`.
- **FIXME comment insertion** — Adding a `FIXME:` comment for a finding judged as `Estimate: Alternative` (see "FIXME insertion for Alternative findings" at the end of this section for details).

Choose a specialist based on the nature of the finding:

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
**Estimate:** {Maintain | Alternative} — Cost: {S/M/L}, Future: {S/M/L}, Signals: {a,b,...} — {basis for the signal selection if Maintain / direction of the FIXME if Alternative}

Procedure:
1. Read the relevant source files and understand the current code and surrounding context.
2. Implement the fix. Follow the project's coding conventions (see CLAUDE.md).
   - **For Maintain:** Apply a regular fix in line with the finding.
   - **For Alternative:** The change is limited to adding a `FIXME:` comment; do not modify logic. Use wording aligned with the FIXME direction recorded in the Estimate line.
3. After the fix, perform a self-review:
   - Re-read the changed code.
   - Verify that the fix addresses the finding and does not introduce new issues.
   - Check edge cases, thread safety, and resource leaks.
   - If the self-review uncovers any problems, fix them before reporting.
4. Report what you changed and why (including any self-review fixes).

**Forbidden:**
- Do not run build commands (`cmake`, `make`, `build.ps1`, `pwsh` build scripts, etc.).
- Do not run formatters (`clang-format`, `cmake-format`, etc.).
- The leader will batch and run those in Step 5. Running builds/formatters concurrently with other parallel-launched fix agents would crosswire the results due to build-cache contention or file-rewrite races.
- Confine your work to source-code edits and leave build success or formatter diffs for the leader to confirm.

For comments, follow the rules in `.claude/rules/comment.md` (auto-loaded).
```

### FIXME Insertion for Alternative Findings (Special Form of Fix)

For findings judged as `Estimate: Alternative`, this step adds a short `FIXME:` comment at the relevant site. The specialist prompt must explicitly state "the change is limited to adding a FIXME comment; do not modify logic" and "use wording aligned with the FIXME direction recorded in the Estimate line". This is treated as a form of fix; Step 6 appends a `Status: 🟢 Fixed` line.

### Parallelization

- Group findings by file to avoid edit conflicts.
- Findings affecting **different files** can be fixed in parallel by launching multiple agents simultaneously.
- Findings affecting the **same file** must be fixed sequentially — launch them one at a time, waiting for each to complete before starting the next.

## Step 5 — Format Verification & Build Verification

After all fixes are complete, run code-format verification and a build to validate the quality of the changes.

### 5a. Format Verification

1. Identify the source files (`.cpp`, `.hpp`, `.h`, `.c`) modified in Step 4.
2. Run `clang-format` on each file to check for format violations:
   ```bash
   clang-format -style=file -fallback-style=none --dry-run -Werror <file>
   ```
3. If there are format violations:
   - Auto-fix with `clang-format -i -style=file -fallback-style=none <file>`.
   - Inspect the resulting diff and verify it does not include unintended changes.
4. If CMake files (`CMakeLists.txt`, `*.cmake`) were modified, verify and fix them similarly with `cmake-format`.

### 5b. Build Verification

1. Identify the build command appropriate for the current platform. Check the build script (`build.ps1`, `Makefile`, etc.) or use the CMake preset documented in `CLAUDE.md`.
2. Run the build. If the build fails:
   - Analyze the error output.
   - Delegate the fix to the appropriate specialist agent (use the same selection criteria and the same prompt template as Step 4, propagating the prohibition on running builds and formatters).
   - Re-run the build after the fix. The leader (you) performs the re-run. Repeat until it succeeds.
3. Report the build result (success, or the errors encountered and how they were resolved).

## Step 6 — Updating the Review Document

For findings fixed under `Estimate: Maintain` and findings that received a FIXME under `Estimate: Alternative`, leave the existing Triage and Estimate lines in place and append the following Status line at the end:

```
> - **{finding-id} Status: 🟢 Fixed** — {Concise description of the fix.}
```

- `Triage: 🚫 Won't Fix` lines were already written by the Step 2 triage sub-agent and are not appended again here.
- `Estimate: 🔻 Downgrade` lines were already written in Step 3 and, since no fix is performed, no `Status: 🟢 Fixed` is appended either.
- For Alternative findings, the `Status: 🟢 Fixed` description should make it clear that a FIXME was inserted, e.g., "Added FIXME comment at {file:line}".

Write the description in the same language as the review document.

## Step 7 — Summary

Output a summary of all actions taken:

```
# Review Response Summary

**Review document:** {path}
**Date:** YYYY-MM-DD

## Results

| # | Severity | Verdict | Specialist | Notes |
|---|----------|---------|------------|-------|
| C-1 | Critical | 🟢 Fixed (Maintain) | cpp-sensei | Concise description of fix |
| M-1 | Major | 🟢 Fixed (Alternative) | obs-sensei | FIXME added (output.cpp:200) + recommend separate PR |
| M-2 | Major | 🔻 Downgrade | qt-sensei | Cost L, Future M, Signals a,b,c. Recommend separate PR. |
| M-3 | Major | 🚫 Won't Fix | — | Existing code, out of scope for this round |
| m-1 | Minor | 🚫 Won't Fix | — | Reason |
| ... | ... | ... | ... | ... |

## Statistics

- **Findings processed:** N
- **🟢 Fixed (Maintain fix):** N
- **🟢 Fixed (Alternative FIXME insertion):** N
- **🔻 Downgrade (no fix):** N
- **🚫 Won't Fix:** N
- **Resolved (skipped):** N
```
