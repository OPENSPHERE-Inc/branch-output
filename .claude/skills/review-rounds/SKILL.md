---
name: review-rounds
description: Automatically iterate parallel review, respond, and resolve across multiple rounds until no actionable findings remain
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*), Bash(git branch:*), Bash(mkdir:*)
---

# Automated Review Rounds

You are the **review round orchestrator**. Your role is to automatically iterate the parallel-review / review-respond / review-resolve flow across multiple rounds, repeating until no actionable findings remain. See "Sub-Agent Usage Rules" for the division of responsibilities between sub-agent delegation and what the orchestrator handles itself.

## Input

The user may optionally specify an output base path. If the argument is `$ARGUMENTS`, interpret it as the output base path (and any options). If no output base path is specified, default to `.claude/tmp/` at the project root.

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--confirm-triage` | OFF | After triage, wait for user confirmation before proceeding to fixes |
| `--confirm-round` | OFF | After review-resolve, if unresolved findings remain, wait for user confirmation before proceeding to the next round |
| `--commit` | OFF | Create a git commit after each finding fix (the orchestrator performs aggregated commits) |
| `--max-rounds N` | 5 | Change the maximum number of outer-loop rounds (1–10) |
| `--base {branch}` | `main` or `master` | Specify the base branch (passed through to parallel-review) |

## Review Document File Naming

- **Format:** `{base-path}/{branch-dir}/review-round{N}.md`
- **Branch name retrieval:** Use `git branch --show-current` to obtain the current branch name.
- **Branch name as directory path** — The whole branch name (including `/`) becomes the directory hierarchy verbatim.
- **Sequential suffixing on re-runs of the same branch:** If `{base-path}/{branch-name}` already exists, create a new directory with a numeric suffix `{branch-name}_1`, `{branch-name}_2`, ... so existing review results are not overwritten.
  - Suffix selection: probe `{branch-name}_1`, `{branch-name}_2`, ... in order and use the smallest suffix that does not yet exist.
- **Examples:**
  - First run: branch `feat/add-replay` → `{base-path}/feat/add-replay/review-round1.md`
  - Second run: `feat/add-replay` already exists → `{base-path}/feat/add-replay_1/review-round1.md`
  - Third run: `feat/add-replay_1` also exists → `{base-path}/feat/add-replay_2/review-round1.md`
  - Branch `dev` → `{base-path}/dev/review-round1.md` (first run), `{base-path}/dev_1/review-round1.md` (second run)
- **Default base-path:** `.claude/tmp/`
- Create directories as needed.
- Keep all per-round review documents; do not overwrite them.

## Review Document Language

Write the review document in **the user's chat language**. If the user is conversing in Japanese, write it in Japanese; if in English, write it in English.

## Sub-Agent Usage Rules

- **No nesting of sub-agents** — If you yourself were launched as a sub-agent, you cannot launch further sub-agents from within.
- **Tasks that themselves do not launch sub-agents may be delegated to sub-agents** (one level of nesting is allowed):
  - **Individual reviewers (Step 2.1)** — Launch cpp-sensei / qt-sensei / obs-sensei / network-sensei, etc. in parallel for individual reviews.
  - **Triage / review-document edits (Step 2.2 / 2.4)** — Have a separate context perform the decision and edits to avoid bias.
  - **Individual fixes (Step 2.2 / 2.4)** — Delegate each finding to the appropriate specialist sub-agent.
  - **review-resolve verification (Step 2.3 / 2.4)** — A read-only verification task.
- Conversely, **aggregation and orchestration responsibilities stay with the orchestrator (you yourself)**:
  - **parallel-review aggregation (Step 2.1)** — Aggregate results from individual reviewers into a report. Do not delegate the entire parallel-review skill to a separate agent (because that would launch further sub-agents).
  - **review-respond leader role (Step 2.2 / 2.4)** — Aggregate the post-triage delegation of fixes, plus format verification, build verification, review-document update, and commit. Do not delegate the entire review-respond skill to a separate agent.
- Per-round results are propagated to subsequent steps and rounds **only via the review document**.

## Flow Overview

```
Round 1 starts
  ├─ 2.1 parallel-review leader role (orchestrator)
  │     [specialist reviewers] launched in parallel → round1.md generated (untriaged)
  ├─ 2.2 review-respond leader role (orchestrator)
  │     [Triage A] → if 0 Will Fix, end the round
  │     [specialist agents] fix each Will Fix
  │     orchestrator performs format verification, build verification, document update, and commit
  ├─ 2.3 review-resolve ([Verify B] sub-agent) → verifies round1.md
  ├─ 2.4 Feedback re-fix loop (up to 3 iterations)
  │     If any feedback: [Triage C] → [specialist agents] re-fix → [Verify D]
  └─ 2.5 Round end → evaluate condition for proceeding to the next round
Round 2 starts (the previous round's review document is not passed in)
  └─ ...
```

## Step 1 — Initialization

1. Verify the existence of the output directory; create it if it does not exist.
2. Obtain the current branch name.
3. Parse the options.
4. Set the round counter to 1.

## Step 2 — Round Loop

While the round counter is less than or equal to `--max-rounds`, repeat the following.

### 2.1 — Review Execution (parallel-review)

The orchestrator (you) directly takes on the "review leader" role of parallel-review. Reviewer roster, reviewer prompt template, severity labels, report format, and format rules follow each section of `.claude/skills/parallel-review/SKILL.md`.

**Procedure:**

1. Print to console: `## Round {N} — Step 1: Parallel Review`
2. Following the reviewer selection criteria in parallel-review § Reviewers, **launch reviewers in parallel** via the Agent tool. Use the template in parallel-review § Step 2 — Launching Parallel Reviewers as the prompt.
3. Following the procedure and report format in parallel-review § Step 3 — Report Integration, aggregate the results and emit the review document (file path: {this round's file path}; language: the user's chat language).

**Round-specific overrides:**

- **Do not pass the previous round's review document to reviewers** — to avoid letting prior judgments bias the new round.
- **Do not deduplicate against previous rounds** — already-resolved findings have been reflected in source code, so they are assumed not to be re-detected.
- **Convergence-induction prevention — never include the following in reviewer prompts:**
  - Counts of past-round findings, count trends, or claims like "things are converging."
  - Past-round finding IDs (`C-1`, `M-1`, etc.).
  - Past-round Fixed / Won't Fix counts or other statistics.
  - Instructions to make reviews stricter or looser as rounds progress.
- **Excluding repeatedly-rejected findings:** Only **findings rejected as Won't Fix in two or more consecutive rounds** may be explicitly listed for exclusion in the reviewer prompt. **A finding marked Won't Fix only once is not excluded** (to leave room for re-judgment). Use a **short summary of the finding content only** for the exclusion notice; do not use IDs (e.g., "the finding about XX in YY function in ZZ file (Won't Fix in the past 2 rounds)").

### 2.2 — Review Response (review-respond)

The orchestrator (you) directly takes on the "review response leader" role of review-respond. Refer to `.claude/skills/review-respond/SKILL.md` for detailed procedures, formats, and prompt templates.

**Input document:** {this round's file path}

**Responsibility breakdown:**

- **Step 1 (Parse) · Step 4 (Format & Build Verification) · Step 5 (Document Update) · Step 6 (Summary)** — Performed directly by the orchestrator.
- **Step 2 (Triage)** — Delegated to a single triage sub-agent following review-respond § Step 2.
- **Step 3 (Fix)** — Each Will Fix finding delegated to the appropriate specialist sub-agent following review-respond § Step 3 (parallelization rules also follow that skill).

**Round-specific overrides:**

- **Progress display (console output):**
  - At triage start: `## Round {N} — Step 2: Triage`
  - At fix / verify / update / commit start: `## Round {N} — Step 3: Review Respond (Fix & Verify)`
- **Additional constraints for the triage sub-agent (Step 2):**
  - Do not reference the previous round's review document (to avoid bias).
  - Explicitly state the Will Fix count in the report (including when zero).
- **Round-loop control after triage:**
  - **0 Will Fix findings:** Skip Step 3 onward; end the round loop → proceed to Step 3 (Final Report).
  - **1 or more Will Fix findings:** Proceed to the fix phase. If `--confirm-triage` is enabled, wait for user confirmation.

### 2.3 — Review Verification (review-resolve)

**Agent launch procedure:**

1. Print to console: `## Round {N} — Step 4: Review Resolve`
2. Launch a **new sub-agent** via the Agent tool. Specify the skill file and arguments explicitly in the prompt:

```
Verify the resolution status of the review document by following the instructions in the skill file below.

Skill file: .claude/skills/review-resolve/SKILL.md
Argument: {this round's file path}
```

### 2.4 — Feedback Check and Re-fix Loop

Read the verification result from Step 2.3 and check whether any findings require feedback.

- **No feedback:** End the round (proceed to 2.5).
- **Feedback exists:** Enter the re-fix loop (up to 3 iterations).

**Re-fix loop (up to 3 iterations):**

1. Print to console: `## Round {N} — Step 4: Feedback Triage (attempt {M}/3)`
   Use the same triage sub-agent launch method as Step 2.2 (review-respond § Step 2 — Triage). Append the following to the "additional constraints" portion of the prompt:
   ```
   Prioritize the Feedback lines in the review document (`> - **{id} Feedback:** ...`) as triage signals. Focus the triage on findings that have a Feedback line.
   ```

2. Print to console: `## Round {N} — Step 4: Feedback Fix (attempt {M}/3)`
   Use the same fix / verification / update method as Step 2.2 (review-respond § Step 3 through Step 5). Append the following to the prompt for each specialist:
   ```
   Re-fix taking into account the Feedback lines in the review document (`> - **{id} Feedback:** ...`).
   ```

3. Print to console: `## Round {N} — Step 4: Feedback Verify (attempt {M}/3)`
   Re-run review-resolve using the same method as Step 2.3.

4. If feedback remains, return to step 1. If 3 iterations do not resolve it, record it as unresolved and end the round.

5. If `--confirm-round` is enabled and unresolved items remain, wait for user confirmation before proceeding to the next round.

### 2.5 — Round End

Record the round's results:
- **Actionable findings count** — Number of findings triaged as Will Fix in Step 2.2.
- **Fixed count** — Number of findings fixed in Step 2.2 and judged **Resolved** or `✓ Verified` by the verification in Step 2.3 / 2.4.
- **Unresolved count** — Number of findings that still have Feedback after 3 iterations of the re-fix loop in Step 2.4 (those triaged as Will Fix but not resolved within this round).

**Conditions to proceed to the next round:** Increment the round counter and return to Step 2.1 only if **all** of the following are satisfied:

1. The round counter is less than or equal to `--max-rounds`.
2. At least one line of source code was changed in this round.

If any of the above is not satisfied, proceed to Step 3 (Final Report). The intent of condition 2: if any source code change occurred, the next round should re-review it. Conversely, if no change was made at all, the next round would run against the same target in the same state and would only surface the same findings — a meaningless repetition.

## Step 3 — Final Report

After all rounds end, produce the final report. File path: `{base-path}/{branch-dir}/final-report.md`

**You yourself** create the final report by reading the review documents from all rounds (do not delegate to an agent). Each review document carries the Status lines for the fix state and the post-verification Feedback / ✓ Verified annotations, so the information can be retrieved from there (note that the review-resolve verification reports are console output only and are not saved to file).

### Final Report Format

```markdown
# Code Review Final Report

**Branch:** {branch-name}
**Date:** YYYY-MM-DD
**Rounds executed:** {N}
**Termination reason:** {No actionable findings / Maximum rounds reached / User stopped}

## Statistics Summary

| Round | Findings | Actionable | Fixed | Not fixed | Feedback re-fixes |
|-------|----------|------------|-------|-----------|-------------------|
| Round 1 | ... | ... | ... | ... | ... |
| Round 2 | ... | ... | ... | ... | ... |
| **Total** | ... | ... | ... | ... | ... |

## All Findings and Resolution Status

### Resolved

| # | Round | Severity | Location | Finding Summary | Resolution |
|---|-------|----------|----------|-----------------|------------|
| 1 | Round 1 | Critical | file:line | Summary | Fixed — description |
| ... | ... | ... | ... | ... | ... |

### Unresolved

| # | Round | Severity | Location | Finding Summary | Status |
|---|-------|----------|----------|-----------------|--------|
| 1 | Round 2 | Major | file:line | Summary | Not resolved even after feedback re-fix |
| ... | ... | ... | ... | ... | ... |

### Decided as No Action Needed

Among Won't Fix findings, list here those **without a recommendation to address in a separate PR** (false positives, preference issues, fully-rejected out-of-scope items, etc.).

| # | Round | Severity | Location | Finding Summary | Reason |
|---|-------|----------|----------|-----------------|--------|
| 1 | Round 1 | Minor | file:line | Summary | Won't Fix — reason |
| ... | ... | ... | ... | ... | ... |

## Recommended for Future Action

Among Won't Fix findings, list here those for which **addressing in a separate PR is recommended** (existing-code bugs, out-of-scope improvement opportunities, future refactoring candidates, etc.). This is mutually exclusive with "Decided as No Action Needed"; a finding must not appear in both.

| # | Severity | Location | Summary | Reason for Recommendation |
|---|----------|----------|---------|---------------------------|
| 1 | Minor | file:line | Summary | Reason |
| ... | ... | ... | ... | ... |

## Review Document Index

| Round | Review | Verification |
|-------|--------|--------------|
| Round 1 | `{path}` | `{path}` |
| Round 2 | `{path}` | `{path}` |
```

## Step 4 — Completion Report

Report the final report path to the user and concisely communicate the key statistics (total findings, resolved count, unresolved count).
