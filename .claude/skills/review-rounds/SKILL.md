---
name: review-rounds
description: Automatically iterate parallel review, respond, and resolve across multiple rounds until no actionable findings remain
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*), Bash(git branch:*), Bash(mkdir:*)
---

# Automated Review Rounds

You are the **review round orchestrator**. Your role is to automatically iterate the parallel-review / review-respond / review-resolve flow across multiple rounds, comprehensively discovering and fixing important issues. See "Sub-Agent Usage Rules" for the division of responsibilities between sub-agent delegation and what the orchestrator handles itself.

The review round orchestrator does not act as a reviewer or fix agent; the role is strictly to orchestrate, aggregate, and judge the overall round process. All reviewer and fix work is delegated to sub-agents.

## Input

The user may optionally specify an output base path. If the argument is `$ARGUMENTS`, interpret it as the output base path (and any options). If no output base path is specified, default to `.claude/tmp/` at the project root.

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--confirm` | OFF | Wait for user confirmation immediately after estimates are gathered (before proceeding to fixes) |
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
- **Tasks that themselves do not launch sub-agents may be delegated to subagents** (one level of nesting is allowed):
  - **Individual reviewers (Step 2.1)** — Launch cpp-sensei / qt-sensei / obs-sensei / network-sensei, etc. in parallel for individual reviews.
  - **Triage / review-document edits (Step 2.2 / 2.4)** — Have a separate context perform the decision and edits to avoid bias.
  - **Individual estimates (Step 2.2 / 2.4)** — Delegate each Will Fix finding to its assigned specialist subagent in parallel (a read-only investigation and verdict task).
  - **Individual fixes (Step 2.2 / 2.4)** — Delegate each finding to the appropriate specialist subagent.
  - **review-resolve verification (Step 2.3 / 2.4)** — A read-only verification task.
- Conversely, **aggregation and orchestration responsibilities stay with the orchestrator (you yourself)**:
  - **parallel-review aggregation (Step 2.1)** — Aggregate results from individual reviewers into a report. Do not delegate the entire parallel-review skill to a separate agent (because that would launch further sub-agents).
  - **review-respond leader role (Step 2.2 / 2.4)** — After triage, aggregate fix delegation, format verification, build verification, review-document update, and commit. Do not delegate the entire review-respond skill to a separate agent.
- Per-round results are propagated to subsequent steps and rounds **only via the review document**.

## Flow Overview

```
Round 1 starts
  ├─ 2.1 parallel-review leader role (orchestrator)
  │     [specialist reviewers] launched in parallel → round1.md generated (untriaged)
  ├─ 2.2 review-respond leader role (orchestrator)
  │     [Triage A] → if 0 Will Fix, end the round
  │     [estimate agents] cost-estimate each Will Fix in parallel → Maintain / Downgrade / Alternative
  │       → if both Maintain and Alternative are 0, skip fix and build verification
  │     [specialist agents] fix Maintain findings, add FIXMEs for Alternative findings
  │     orchestrator performs format verification, build verification, document update, and commit
  ├─ 2.3 review-resolve ([Verify B] sub-agent) → verifies round1.md
  ├─ 2.4 Feedback re-fix loop (up to 3 iterations)
  │     If any feedback: [Triage C] → [estimate agents] → [specialist agents] re-fix → [Verify D]
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
2. Following the reviewer selection criteria in parallel-review § Reviewers, **launch reviewers in parallel** via the Agent tool. Use the template in parallel-review § Step 2 as the prompt.
3. Following the procedure and report format in parallel-review § Step 3, aggregate the results and emit the review document (file path: {this round's file path}; language: the user's chat language).

**Round-specific overrides:**

- **Do not pass the previous round's review document to reviewers** — to avoid letting prior judgments bias the new round.
- **Do not deduplicate against previous rounds** — already-resolved findings have been reflected in source code, so they are assumed not to be re-detected.
- **Convergence-induction prevention** — Review rounds aim to thoroughly examine the diff and surface important issues; steering toward a smaller finding count is an anti-pattern.
  - **Never include the following in reviewer prompts:**
    - Counts of past-round findings, count trends, or claims like "things are converging."
    - Past-round finding IDs (`C-1`, `M-1`, etc.).
    - Past-round Fixed / Won't Fix counts or other statistics.
  - Omitting parts of the reviewer prompt template, or appending instructions that try to control finding counts, is prohibited.
  - The orchestrator must not introduce findings of its own beyond those submitted by reviewers.

### 2.2 — Review Response (review-respond)

The orchestrator (you) directly takes on the "review response leader" role of review-respond. Refer to `.claude/skills/review-respond/SKILL.md` for detailed procedures, formats, and prompt templates.

**Input document:** {this round's file path}

**Responsibility breakdown:**

- **Step 1 (Parse) · Step 5 (Format & Build Verification) · Step 6 (Document Update) · Step 7 (Summary)** — Performed directly by the orchestrator.
- **Step 2 (Triage)** — Delegated to a single triage subagent following review-respond § Step 2.
- **Step 3 (Estimate)** — Each Will Fix finding delegated in parallel to its assigned specialist subagent following review-respond § Step 3 (a read-only investigation and verdict task).
- **Step 4 (Fix)** — Regular fixes for Maintain findings and FIXME insertion for Alternative findings delegated to the appropriate specialist subagents following review-respond § Step 4 (parallelization rules also follow that skill).

**Round-specific overrides:**

- **Progress display (console output):**
  - At triage start: `## Round {N} — Step 2: Triage`
  - At estimate start: `## Round {N} — Step 2.5: Estimate`
  - At fix / verify / update / commit start: `## Round {N} — Step 3: Review Respond (Fix & Verify)`
- **Additional constraints for the triage subagent (Step 2):**
  - Do not reference the previous round's review document (to avoid bias).
  - Explicitly state the Will Fix count in the report (including when zero).
- **Additional constraints for the estimate subagents (Step 3):**
  - Do not reference the previous round's review document (to avoid bias).
  - When evaluating Spread signal e (Will Fix originating from a FIXME), check whether the finding has its origin in a `FIXME:` / `TODO:` in the review text or in the target files.
- **Round-loop control after triage:**
  - **0 Will Fix findings:** Skip Steps 2.3–2.4 (verification and feedback re-fix) and proceed to Step 2.5 (Round End). No source code changes have occurred this round, so the condition for proceeding to the next round is not satisfied and the flow advances to the final report.
  - **1 or more Will Fix findings:** Proceed to the estimate phase.
- **Round-loop control after estimate:**
  - **Both Maintain and Alternative are 0 (all Downgrade):** Skip the fix and build-verification phases; perform only the document update (review-respond § Step 6) and end the round.
  - **1 or more Maintain or Alternative findings:** Proceed to the fix phase (regular fixes for Maintain, FIXME insertion for Alternative). If `--confirm` is enabled, display the estimate result and wait for user confirmation.

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

**Feedback re-fix loop (up to 3 iterations):**

1. Print to console: `## Round {N} — Step 4.1: Feedback Triage (attempt {M}/3)`
   Use the same triage subagent launch method as Step 2.2 (review-respond § Step 2). Append the following to the "additional constraints" portion of the prompt:
   ```
   Prioritize findings whose Verification field between the metadata markers shows "💬 Feedback — ..." as triage signals. Focus the triage on findings that have a Feedback annotation.
   ```

2. Print to console: `## Round {N} — Step 4.2: Feedback Estimate (attempt {M}/3)`
   Run estimates for each Will Fix using the same method as Step 2.2 (review-respond § Step 3). Append the following to the prompt for each estimate agent:
   ```
   For findings whose Verification field between the metadata markers shows "💬 Feedback — ...", perform the cost estimate taking that content into account. If the feedback inflates the fix cost, consider Downgrade.
   ```
   If every finding is Downgraded, skip step 3 and proceed to step 4.

3. Print to console: `## Round {N} — Step 4.3: Feedback Fix (attempt {M}/3)`
   Use the same fix / verification / update method as Step 2.2 (review-respond § Step 4 through Step 6). Append the following to the prompt for each specialist:
   ```
   Re-fix the findings whose Verification field between the metadata markers shows "💬 Feedback — ...", taking that content into account.
   ```

4. Print to console: `## Round {N} — Step 4.4: Feedback Verify (attempt {M}/3)`
   Re-run review-resolve using the same method as Step 2.3.

5. If feedback remains, return to step 1. If 3 iterations do not resolve it, end the round (any remaining 💬 Feedback annotations are preserved as-is and counted as "unresolved" in Step 2.5).

6. If `--confirm-round` is enabled and unresolved items remain, wait for user confirmation before proceeding to the next round.

### 2.5 — Round End

Record the round's results:
- **Actionable findings count** — Number of findings triaged as Will Fix in Step 2.2.
- **Maintain count** — Number of findings judged Maintain at estimate (subject to regular fixing).
- **Alternative count** — Number of findings judged Alternative at estimate (subject to FIXME insertion).
- **Downgrade count** — Number of findings judged Downgrade at estimate (no fix performed; if separate-PR recommendation is included, record that subset count as a breakdown).
- **Fixed count** — Number of findings that were fixed in Step 2.2 (including regular fixes for Maintain and FIXME insertions for Alternative) and judged **Resolved** by the verification in Step 2.3 / 2.4 (`✅ Verified` is recorded in the Verification field).
- **Unresolved count** — Number of findings whose Verification field still shows `💬 Feedback` after 3 iterations of the re-fix loop in Step 2.4 (those triaged as Will Fix but not resolved within this round).

**Conditions to proceed to the next round:** Increment the round counter and return to Step 2.1 only if **all** of the following are satisfied:

1. The round counter is less than or equal to `--max-rounds`.
2. At least one line of source code was changed in this round.

If any of the above is not satisfied, proceed to Step 3 (Final Report) below. The intent of condition 2: if any source code change occurred, the next round should re-review it. Conversely, if no change was made at all, the next round would run against the same target in the same state and would only surface the same findings — a meaningless repetition.

## Step 3 — Final Report

After all rounds end, produce the final report. File path: `{base-path}/{branch-dir}/final-report.md`

**You yourself** create the final report by reading the review documents from all rounds (do not delegate to an agent). Each review document carries the Triage / Estimate / Status / Verification fields between the metadata markers for every finding, so the information can be retrieved from there (note that the review-resolve verification reports are console output only and are not saved to file).

### Final Report Format

```markdown
# Code Review Final Report

**Branch:** {branch-name}
**Date:** YYYY-MM-DD
**Rounds executed:** {N}
**Termination reason:** {No actionable findings / Maximum rounds reached / User stopped}

## Statistics Summary

| Round | Findings | 🔧 Will Fix | ▶️ Maintain | 🚧 Alternative | 🔻 Downgrade | 🟢 Fixed | Unresolved | Feedback re-fixes |
|-------|----------|------------|-------------|----------------|--------------|----------|------------|-------------------|
| Round 1 | ... | ... | ... | ... | ... | ... | ... | ... |
| Round 2 | ... | ... | ... | ... | ... | ... | ... | ... |
| **Total** | ... | ... | ... | ... | ... | ... | ... | ... |

Column definitions:
- **Unresolved**: Number of findings that still have Feedback after 3 iterations of the re-fix loop in Step 2.4 (synonymous with [Step 2.5 § Unresolved count](#25--round-end)).
- **Feedback re-fixes**: Number of feedback re-fix loop iterations executed in this round (max 3).

## All Findings and Resolution Status

### Resolved

Aggregate all findings with `Status: 🟢 Fixed` (covers both regular Maintain fixes and Alternative FIXME insertions).

| # | Round | Severity | Location | Finding Summary | Resolution |
|---|-------|----------|----------|-----------------|------------|
| 1 | Round 1 | Critical | file:line | Summary | 🟢 Fixed (Maintain) — fix description |
| 2 | Round 1 | Major | file:line | Summary | 🟢 Fixed (Alternative) — FIXME comment added at {file:line} |
| ... | ... | ... | ... | ... | ... |

### Unresolved

| # | Round | Severity | Location | Finding Summary | Status |
|---|-------|----------|----------|-----------------|--------|
| 1 | Round 2 | Major | file:line | Summary | Not resolved even after feedback re-fix |
| ... | ... | ... | ... | ... | ... |

### Decided as No Action Needed

List here all `Triage: 🚫 Won't Fix` and `Estimate: 🔻 Downgrade` findings that **do not carry a separate-PR recommendation** (false positives, preference issues, fully-rejected out-of-scope items, Downgrades with neither alternative response nor separate-PR recommendation, etc.).

| # | Round | Severity | Location | Finding Summary | Reason |
|---|-------|----------|----------|-----------------|--------|
| 1 | Round 1 | Minor | file:line | Summary | Triage: 🚫 Won't Fix — reason |
| 2 | Round 1 | Minor | file:line | Summary | Estimate: 🔻 Downgrade — Cost L, Signals a,b. Neither alternative response nor separate-PR recommendation. |
| ... | ... | ... | ... | ... | ... |

## Recommended for Future Action

Aggregate findings whose follow-up in a separate PR is anticipated:

- `Triage: 🚫 Won't Fix` findings whose rationale explicitly recommends a separate PR
- `Estimate: 🔻 Downgrade` findings whose rationale explicitly recommends a separate PR
- `Estimate: 🚧 Alternative` (FIXME already inserted) findings whose rationale explicitly recommends a separate PR

This is mutually exclusive with "Decided as No Action Needed". Alternative FIXME insertions are also listed in the "Resolved" section, but if they carry a separate-PR recommendation, list them additionally here (for roadmap purposes).

| # | Severity | Location | Summary | Reason for Recommendation |
|---|----------|----------|---------|---------------------------|
| 1 | Minor | file:line | Summary | Triage: 🚫 Won't Fix — existing-code bug. Recommend addressing in a separate PR. |
| 2 | Major | file:line | Summary | Estimate: 🔻 Downgrade — Cost L, Signals a,b,c. Recommend addressing in a separate PR. |
| 3 | Major | file:line | Summary | Estimate: 🚧 Alternative — FIXME already added (output.cpp:200). Recommend full fix in a separate PR. |
| ... | ... | ... | ... | ... |

## Review Document Index

| Round | Review | Verification |
|-------|--------|--------------|
| Round 1 | `{path}` | `{path}` |
| Round 2 | `{path}` | `{path}` |
```

## Step 4 — Completion Report

Report the final report path to the user and concisely communicate the key statistics (total findings, resolved count, unresolved count).
