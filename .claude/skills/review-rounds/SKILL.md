---
name: review-rounds
description: Automatically iterate parallel review, respond, and resolve across multiple rounds until no actionable findings remain
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*), Bash(git branch:*), Bash(mkdir:*)
---

# Automated Review Rounds

You are the **review round orchestrator**. Your role is to automatically iterate the parallel-review / review-respond / review-resolve workflow across multiple rounds until no actionable findings remain.

**Role separation:** Delegate individual reviews, triage, fixes, and verification to subagents whenever possible. However, consolidation of subagent results (parallel-review aggregation), format verification, build verification, final review document updates, and commits are performed by the orchestrator itself (see "Subagent Usage Rules" for details).

## Input

The user may optionally specify an output base path. If the argument is `$ARGUMENTS`, interpret it as the output base path (and options). If no output base path is specified, default to `.claude/tmp/` under the project root.

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--confirm-triage` | OFF | Wait for user confirmation after triage before proceeding to fixes |
| `--confirm-round` | OFF | Wait for user confirmation before proceeding to the next round when unresolved findings exist |
| `--commit` | OFF | Create a git commit after each finding is fixed (orchestrator performs consolidated commits) |
| `--max-rounds N` | 5 | Maximum number of outer loop rounds (1–10) |
| `--base {branch}` | `main` or `master` | Base branch for comparison (passed to parallel-review) |

## Review Document Naming

- **Format:** `{base-path}/{branch-dir}/review-round{N}.md`
- **Branch name:** Obtained via `git branch --show-current`.
- **Branch name is used as a directory path** — the entire branch name (including `/`) becomes the directory hierarchy.
- **Suffix numbering for re-runs on the same branch:** If the `{base-path}/{branch-name}` directory already exists, append a suffix (`{branch-name}_1`, `{branch-name}_2`, ...) to create a new directory. This preserves existing review results.
  - Suffix selection: Check from `{branch-name}_1` upward and use the smallest number that does not yet exist.
- **Examples:**
  - First run: branch `feat/add-replay` → `{base-path}/feat/add-replay/review-round1.md`
  - Second run: `feat/add-replay` already exists → `{base-path}/feat/add-replay_1/review-round1.md`
  - Third run: `feat/add-replay_1` also exists → `{base-path}/feat/add-replay_2/review-round1.md`
  - Branch `dev` → `{base-path}/dev/review-round1.md` (first), `{base-path}/dev_1/review-round1.md` (second)
- **Default base-path:** `.claude/tmp/`
- Create directories as needed.
- Retain all round review documents — do not overwrite.

## Review Document Language

Write review documents in the **user's chat language**. If the user is communicating in Japanese, write in Japanese; if in English, write in English.

## Subagent Usage Rules

- **Subagent nesting is prohibited** — If you are running as a subagent yourself, you cannot launch further subagents from within.
- **Tasks that do not launch further subagents themselves may be delegated to a subagent** (one level of nesting is acceptable):
  - **Individual reviewers (Step 2.1)** — Launch cpp-sensei / qt-sensei / obs-sensei / network-sensei etc. in parallel to review individually.
  - **Triage and review document editing (Step 2.2 / 2.5)** — Running in a separate context avoids orchestrator bias.
  - **Individual fixes (Step 2.3 / 2.5)** — Delegate each finding to the appropriate specialist subagent.
  - **Review-resolve verification (Step 2.4 / 2.5)** — Read-only verification task.
- On the other hand, **aggregation and orchestration roles are performed by the orchestrator (you) directly**:
  - **Parallel-review consolidation (Step 2.1)** — Aggregate individual reviewer results into a report. Do NOT delegate the entire parallel-review skill to another agent (that would trigger further subagent nesting).
  - **Format verification, build verification, review document updates, and commits (Step 2.3 / 2.5)** — Consolidated after all fixes complete.
- Results from each round are passed forward **only through review documents**.

## Flow Overview

```
Round 1 Start
  ├─ Orchestrator acts as parallel-review lead, launching reviewers
  │   (cpp-sensei, qt-sensei, obs-sensei, network-sensei, etc.) in parallel
  │   → round1.md generated (reviewer-level dedup only, triage not done)
  ├─ [Agent A] Triage & document editing
  │   → round1.md edited, Will Fix findings identified
  │   (if no Will Fix, end round)
  ├─ [Specialist agents] Each Will Fix finding fixed in parallel / sequentially
  ├─ Orchestrator runs format verification, build verification, document update, commit
  │   → round1.md updated
  ├─ [Agent B] review-resolve → round1.md verified
  ├─ Orchestrator checks feedback
  │   └─ If feedback: [Agent C] re-triage → [specialist agents] re-fix (up to 3 times)
  │   └─ [Agent D] review-resolve → round1.md re-verified
  └─ Round 1 End
Round 2 Start
  ├─ Orchestrator launches reviewers in parallel (fresh reviewer contexts)
  │   → round2.md generated
  ├─ [Agent E] Triage & document editing (prior round docs NOT passed)
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

Regardless of the round, always review the entire target scope. Do NOT pass prior round review documents to the review agents (to avoid bias from prior-round judgments). Deduplication against prior rounds is **not** performed as a rule — resolved findings are already reflected in the source code and should not be re-detected, and handling of repeatedly rejected findings is covered by "Exclusion of repeatedly rejected findings" below.

The orchestrator (you) acts as the parallel-review lead directly, launching individual reviewers in parallel and consolidating the report. Follow the review rules in `.claude/skills/parallel-review/SKILL.md` (required/optional reviewers, severity classification, report format, etc.).

**Exclusion of repeatedly rejected findings:** Only **findings that have been raised across 2 or more rounds and rejected (Won't Fix / No Action Needed) each time** may be explicitly listed as exclusions in the reviewer's prompt. **Do NOT exclude findings rejected only once** (the judgment may change on the second review, so the finding must be surfaced again).

**Execution procedure:**

1. Print to console: `## Round {N} — Step 1: Parallel Review`
2. Launch reviewer agents **in parallel** via the Agent tool (cpp-sensei / qt-sensei / obs-sensei / network-sensei are required; add av-sensei / devops-sensei / python-sensei / lua-sensei as needed based on scope). Provide each reviewer with:
   - Review target: Commits unique to the current branch and working tree changes
   - Their specialist perspective
   - Read-only instruction, outputting findings with severity labels (Critical / Major / Minor / Info)
   - {If there are repeatedly rejected findings only:} List of such findings to exclude, with summary and round numbers of rejection
3. Aggregate all reviewer results and write the review document following parallel-review's report format:
   - File path: {current round file path} (language: user's chat language)
   - **Merge reviewer-level duplicates** (if multiple reviewers report the same finding, merge into one entry and note which reviewers identified it).
   - **Do NOT make the actionable decision (`[Action Required]` / `[No Action Needed]`)** — leave the action line blank or use a placeholder like "(pending triage)" (the decision is made in Step 2.2).
   - **Do NOT perform deduplication against prior rounds** (not needed per above).

### 2.2 — Triage & Review Document Editing

Delegate the task of triaging each finding in the review document generated by Step 2.1 (deciding whether to act) and writing the results back to the review document to a **single specialist subagent**. Running in a separate context avoids bias from the orchestrator's accumulated context and produces fresher judgment.

**Prior round review documents are NOT passed to the subagent:**
- Findings that were resolved in prior rounds are already reflected in the source code, so they should not be re-detected in the review (= deduplication is unnecessary).
- Prior-round judgments could bias the current-round triage and produce incorrect decisions.
- Suppression of repeatedly rejected findings is already handled by the exclusion instruction in Step 2.1's reviewer prompts.

**Execution procedure:**

1. Print to console: `## Round {N} — Step 2: Triage`
2. Launch a **new subagent** via the Agent tool and delegate the following:

```
Triage the findings in the review document and reflect the results in the document.
**This task does NOT modify source code.** Only edit the review document and report triage results.

Review document: {current round file path}

Tasks:
1. **Triage** — For each finding (action line is a placeholder at Step 2.1), classify as one of:
   - **Will Fix** — Valid finding, should be addressed → finalize action line to `**[Action Required]**`
   - **Won't Fix** — Not applicable / false positive / acceptable risk (state the reason) → finalize action line to `**[No Action Needed]**`
   - **Needs Investigation** — Read the relevant source to decide → finalize as Will Fix / Won't Fix

   **Won't Fix guidelines** — Classify as Won't Fix when the finding falls into any of the following:
   - Out of scope of the branch diff.
   - Pre-existing bugs not introduced by this branch.
   - Based on incorrect hypotheses or technically wrong.
   - Acceptable given the project's purpose, use case, or intended users.
   - Refactoring that is merely a matter of preference.

   **High-severity exception:** If a Won't Fix finding is **Critical** or **Major**, recommend addressing it in a separate PR in the rationale (e.g. "Won't Fix — pre-existing bug, recommend fixing in a separate PR").
2. **Assign specialist** — For each Will Fix, assign the most appropriate specialist (cpp-sensei / qt-sensei / obs-sensei / network-sensei / av-sensei / devops-sensei / python-sensei / lua-sensei).
3. **Reflect results in the review document** — Write results back to the review document ({current round file path}). Preserve the existing structure and edit as follows:
   - **Will Fix** findings: Finalize action line to `**[Action Required]**`. Add an HTML comment `<!-- Triage: Will Fix / assignee: cpp-sensei -->` immediately after the action line to note the assignee.
   - **Won't Fix** findings: Finalize action line to `**[No Action Needed]**` and append a status line `> **{id} Status: Won't Fix** — {reason}` immediately after the action line (before the `---` separator).

Report format:
- Triage results:
  | Finding ID | Decision | Assignee | Rationale |
  |------------|----------|----------|-----------|
  | C-1 | Will Fix | cpp-sensei | ... |
  | M-2 | Won't Fix | — | Pre-existing code, out of scope |
- Presence of actionable (Will Fix) findings.
```

3. Receive the results from the subagent.
4. **Actionable check:**
   - **No Will Fix findings:** Exit the round loop and proceed to Step 3 (Final Report).
   - **Will Fix findings exist:** Proceed to the next step.
5. {If --confirm-triage enabled:} Present triage results to the user and wait for confirmation before proceeding. If disabled, proceed without confirmation.

### 2.3 — Respond (Fix)

Delegate each Will Fix finding's fix to the appropriate specialist subagent (one-level nesting is acceptable). After all fixes complete, the orchestrator consolidates format verification, build verification, document update, and commits. See `.claude/skills/review-respond/SKILL.md` from Step 3 onward for detailed rules.

**Execution procedure:**

1. Print to console: `## Round {N} — Step 3: Review Respond (Fix)`
2. For each Will Fix finding from Step 2.2's triage results, launch the assigned specialist subagent via the Agent tool to perform the fix:
   - **Parallelization:** Different files in parallel, same file sequentially (to avoid edit conflicts).
   - Example delegation prompt to each specialist:

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

**Prohibited:**
- Do NOT create git commits (the orchestrator performs consolidated commits).
- Do NOT edit the review document ({current round file path}).
```

3. After all fix agents complete, **the orchestrator** performs the following consolidation:
   - **Format verification** (clang-format / cmake-format) — for all modified files.
   - **Build verification** — Run the platform-appropriate build. If the build fails, relaunch the relevant specialist subagents to fix (instruct self-review on re-fix as well).
   - **Review document update** — For each Will Fix finding that was fixed, add a status line `> **{id} Status: Fixed** — {concise fix description}` immediately after the action line (before the `---` separator). Won't Fix / Acknowledged status lines were already added in Step 2.2, so do NOT add them here.
   - **Commit** — {If --commit enabled:} Commit per finding fix. {If --commit disabled:} Do NOT create git commits. Committing is strictly prohibited.

### 2.4 — Verify (review-resolve)

**Agent launch procedure:**

1. Print to console: `## Round {N} — Step 4: Review Resolve`
2. Launch a **new subagent** via the Agent tool. Specify the skill file and arguments explicitly in the prompt:

```
Verify the resolution status of the review document following the instructions in the skill file below.

Skill file: .claude/skills/review-resolve/SKILL.md
Arguments: {current round file path}
```

### 2.5 — Feedback Check and Re-fix Loop

Read the verification report and check for findings that require feedback.

- **No feedback needed:** Proceed to round end.
- **Feedback needed:** Enter the re-fix loop (up to 3 attempts).

Re-fix loop (up to 3 attempts):

1. Print to console: `## Round {N} — Step 5: Feedback Triage (attempt {M}/3)`
2. Launch a **new subagent** via the Agent tool to triage unresolved findings based on feedback. Example prompt:

```
Triage the unresolved findings in the review document, taking feedback annotations into account.
**This task is triage only.** Do NOT perform fixes. Report triage results only.

Skill file: .claude/skills/review-respond/SKILL.md — "Step 2 — Triage"
Arguments: {current round file path}

Prioritize feedback annotations (`> **{id} Feedback:** ...`) when deciding.
Output format is the same as Step 2.2's triage result table (Finding ID / Decision / Assignee / Rationale).
```

3. Print to console: `## Round {N} — Step 5: Feedback Fix (attempt {M}/3)`
4. Receive the triage results, then perform re-fixes following **the same flow as Step 2.3 procedure 2–3**. However, add the instruction to each specialist: "Re-fix based on the feedback annotations (`> **{id} Feedback:** ...`)".

5. Print to console: `## Round {N} — Step 5: Feedback Verify (attempt {M}/3)`
6. Launch a **new subagent** via the Agent tool to re-run review-resolve:

```
Verify the resolution status of the review document following the instructions in the skill file below.

Skill file: .claude/skills/review-resolve/SKILL.md
Arguments: {current round file path}
```

7. If feedback remains, return to step 1. If unresolved after 3 attempts, record as unresolved and end the round.

8. If `--confirm-round` is enabled and unresolved findings remain, wait for user confirmation before proceeding to the next round.

### 2.6 — Round End

Record the round results:
- **Actionable findings count** — Number of findings classified as Will Fix in Step 2.2.
- **Fix count** — Number of findings fixed in Step 2.3 onward and verified as **Resolved** or `✓ Verified` in Step 2.4 / 2.5.
- **Unresolved count** — Number of findings that still have Feedback remaining after 3 iterations of the Step 2.5 re-fix loop (Will Fix findings that could not be resolved within this round).

**Conditions to proceed to the next round:** Only when **all** of the following are satisfied, increment the round counter and return to Step 2.1:

1. The round counter is ≤ `--max-rounds`.
2. At least one finding was fixed in this round (i.e., fix count ≥ 1, meaning some change was made to source files).

If any condition is not met, proceed to Step 3 (Final Report). The rationale for condition 2 is that if nothing was fixed, the next round would review the same target in the same state and produce the same findings — a pointless repetition.

## Step 3 — Final Report

After all rounds complete, generate a final report. File path: `{base-path}/{branch-dir}/final-report.md`

The final report must be written by **you (the orchestrator)** by reading all round review documents (do NOT delegate to an agent). Each review document has status lines for fix progress and Feedback / ✓ Verified annotations after verification, so obtain information from there (note that review-resolve's verification report is console-output only and is not saved to a file).

### Final Report Format

```markdown
# Code Review Final Report

**Branch:** {branch-name}
**Date:** YYYY-MM-DD
**Rounds completed:** {N}
**Termination reason:** {No actionable findings / Max rounds reached / User stopped}

## Statistics Summary

| Round | Findings | Actionable | Fixed | Unresolved | Feedback Re-fixes |
|-------|----------|------------|-------|------------|-------------------|
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
