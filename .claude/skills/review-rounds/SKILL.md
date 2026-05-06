---
name: review-rounds
description: Automatically iterate parallel review, respond, and resolve across multiple rounds until no actionable findings remain
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*), Bash(git status:*), Bash(git branch:*), Bash(mkdir:*), Bash(cmake:*), Bash(make:*), Bash(pwsh:*), Bash(clang-format:*), Bash(cmake-format:*), Bash(.claude/scripts/rm-tmp.sh:*)
---

# Automated Review Rounds

You are the **review round orchestrator**. Automatically iterate the parallel-review / review-respond / review-resolve flow across multiple rounds, comprehensively discovering and fixing important issues. The orchestrator does not act as a reviewer or fix agent; all such work is delegated to sub-agents. See "Sub-Agent Usage Rules" for the detailed division of responsibilities.

## Input

The user may optionally specify an output base path. If the argument is `$ARGUMENTS`, interpret it as the output base path (and any options). If no output base path is specified, default to `.claude/tmp/` at the project root.

## Options

- `--confirm` (default OFF) — Wait for user confirmation immediately after estimates are gathered (before proceeding to fixes).
- `--confirm-round` (default OFF) — After review-resolve, if unresolved findings remain, wait for user confirmation before proceeding to the next round.
- `--commit` (default OFF) — Create a git commit after each finding fix (the orchestrator performs aggregated commits).
- `--max-rounds N` (default 5, range 1–10) — Change the maximum number of outer-loop rounds.
- `--base {branch}` (default `main` or `master`) — Specify the base branch (passed through to parallel-review).

## Review Document File Naming

- Format: `{base-path}/{branch-dir}/review-round{N}.md`
- Branch name retrieval: use `git branch --show-current` to obtain the current branch name.
- Branch name as directory path — the whole branch name (including `/`) becomes the directory hierarchy verbatim.
- Sequential suffixing on re-runs of the same branch: append `_1`, `_2`, ... (smallest number that does not yet exist).
  - Example: branch `feat/add-replay`, first run → `{base-path}/feat/add-replay/review-round1.md`; re-run → `{base-path}/feat/add-replay_1/review-round1.md`.
- Default base-path: `.claude/tmp/`. Create the directory as needed.

## Review Document Language

Write the review document in the user's chat language.

## Sub-Agent Usage Rules

- **For common prohibitions, see `.claude/rules/sub-agent.md`** — append this file's content to each sub-agent prompt before passing it to the Agent tool.
- **No nesting of sub-agents** — if you yourself were launched as a sub-agent, you cannot launch further sub-agents from within.
- **Most work, including aggregation and consolidation, is delegated to sub-agents** (one level of nesting is allowed):
  - Individual reviewers (Step 2.1) — launch cpp-sensei / qt-sensei / obs-sensei / network-sensei, etc. in parallel for individual reviews. Each reviewer Writes findings to a file; the return value is only the path and counts.
  - Aggregator sub-agent (Step 2.1) — Read the individual reviewer output files and consolidate them into the review document (parallel-review § Step 3).
  - Parsing sub-agent (review-resolve, Steps 2.3 / 2.4) — Read the review document and return verification assignments (by_assignee) (review-resolve § Step 1; no file output).
  - Triage sub-agent (Steps 2.2 / 2.4) — render verdicts in a separate context to avoid bias; Reads the review document directly and performs finding extraction and verdict in one pass (review-respond § Step 1).
  - Individual estimates (Steps 2.2 / 2.4) — delegate in parallel to specialist sub-agents per assignee; each Sub batch-estimates its assigned ids (review-respond § Step 2; read-only).
  - Estimate aggregator sub-agent (Steps 2.2 / 2.4) — generate a summary of individual estimate results (review-respond § Step 2).
  - Individual fixes (Steps 2.2 / 2.4) — delegate to specialist sub-agents per assignee; each Sub sequentially fixes its assigned ids (review-respond § Step 3).
  - Format & build verification sub-agent (Steps 2.2 / 2.4) — run clang-format / cmake-format + build once; on failure, analyze code and determine the responsible specialist (no fix; recommendation only).
  - Build-fix specialist sub-agent (Steps 2.2 / 2.4) — fix build errors as the specialist determined by the format & build verification sub-agent. The leader re-launches the format & build verification sub-agent afterwards (review-respond § Step 4).
  - Verification sub-agent (Steps 2.3 / 2.4) — launched in parallel per specialist; each batch-verifies its assigned findings (review-resolve § Step 2; read-only).
  - Aggregator sub-agent (Steps 2.2 / 2.3 / 2.4) — generate events.jsonl from intermediate files and run render-review.py (review-respond § Step 5 / review-resolve § Step 3).
  - Final-report aggregator sub-agent (Step 3) — generate the final report from all rounds' review documents.
- **The orchestrator (you) directly handles only the following**:
  - Control between steps and the round-loop decision (including the format & build verification sub-agent ⇄ build-fix specialist sub-agent retry loop). Operational data files from sub-agents may be Read; do not Read source code itself.
  - Sub-agent launch and aggregation of return values (lightweight counters, paths, 1-line summaries).
  - Final summary presentation to the user.
- **The orchestrator does not load review-finding or verdict bodies into context.** Hold only file paths and lightweight counters; details are handled by sub-agents.
- Per-round results are propagated to subsequent steps and rounds **only via the review document**. Intermediate data between sub-agents must be self-contained within a step (a single Instruction) and must not persist across steps.

## Flow Overview

```
Round 1 starts
  ├─ 2.1 parallel-review (orchestrator)
  │     [specialist reviewers] launched in parallel → each Writes reviews/{name}.md
  │     [aggregator Sub] Reads each reviews/{name}.md → emits round1.md (untriaged)
  ├─ 2.2 review-respond (orchestrator)
  │     [triage Sub] Reads round1.md → emits triage.json (with by_assignee)
  │     ↳ if 0 Will Fix, end the round
  │     [estimate Sub group] (per-assignee, parallel) batch-estimates assigned ids → emits estimates/{id}.json
  │     [estimate aggregator Sub] estimates/*.json → emits estimate-summary.md
  │     ↳ if both Maintain and Alternative are 0, skip fix and build verification
  │     [fix Sub group] (per-assignee, parallel) Maintain fixes, Alternative FIXME insertions → emits statuses/{id}.json
  │     [format & build verification Sub] ⇄ [build-fix specialist Sub] loop (max 5, leader-controlled)
  │     [aggregator Sub] triage.json + estimates/*.json + statuses/*.json → events.jsonl → render-review.py
  ├─ 2.3 review-resolve
  │     [parsing Sub] Reads round1.md → returns by_assignee (no file output)
  │     [verification Sub group] launched per specialist; each batch-verifies its assigned findings → emits verifications/{id}.json
  │     [aggregator Sub] verifications/*.json → events.jsonl → render-review.py
  ├─ 2.4 Feedback re-fix loop (up to 3 iterations)
  │     [triage Sub] → [estimate Sub group (per-assignee)] → [fix Sub group (per-assignee)]
  │       → [format & build verification Sub] ⇄ [build-fix specialist Sub] loop
  │       → [aggregator Sub] → [verification Sub group] → [aggregator Sub]
  └─ 2.5 Round end → evaluate condition for proceeding to the next round
Round 2 starts (the previous round's review document is not passed in)
  └─ ...
Final step
  └─ [final-report aggregator Sub] all round{N}.md + template → final-report.md
```

## Step 1 — Initialization

1. Verify the existence of the output directory; create it if it does not exist.
2. Obtain the current branch name.
3. Parse the options.
4. Set the round counter to 1.

## Step 2 — Round Loop

While the round counter is less than or equal to `--max-rounds`, repeat the following.

### 2.1 — Review Execution (parallel-review)

The orchestrator (you) directly takes on the "review leader" role of parallel-review. Procedure, templates, and formats follow `.claude/skills/parallel-review/SKILL.md`.

Procedure:

1. Print to console: `## Round {N} — Step 1: Parallel Review`
2. Following parallel-review § Step 1, prepare the working directory and diff file, and launch the scope-analysis sub-agent. Hold only the return value (`line_count` / `recommended_reviewers`) in context.
3. Launch each `recommended_reviewers.name` in parallel via `Agent(subagent_type=name, prompt=...)` (the agent definition's persona and perspective load automatically). Each reviewer Writes findings to `{tmp_dir}/reviews/{name}.md` and returns only `{path, severity counts}`.
4. Following parallel-review § Step 3, launch the aggregator sub-agent to generate the review document (output: this round's file path; language: the user's chat language). Hold only the aggregator sub-agent's return value (`{doc_path, findings_total, severity_counts}`) in context.
5. Following parallel-review § Step 4, remove `{tmp_dir}`.

Round-specific overrides:

- Do not pass the previous round's review document to reviewers (bias avoidance).
- Do not deduplicate against previous rounds.
- Convergence-induction prevention:
  - **Never include the following in reviewer prompts**:
    - Counts of past-round findings, count trends, or claims like "things are converging".
    - Past-round finding IDs (`C-1`, `M-1`, etc.).
    - Past-round Fixed / Won't Fix counts or other statistics.
  - Omitting parts of the reviewer prompt template, or appending instructions that try to control finding counts, is prohibited.
  - The review orchestrator must not introduce findings of its own beyond those submitted by reviewers.

### 2.2 — Review Response (review-respond)

The orchestrator (you) directly takes on the "review response leader" role of review-respond. Procedure and templates follow `.claude/skills/review-respond/SKILL.md`.

Input document: this round's file path

- Step 6 (Summary) — the orchestrator displays the `summary_line` received from the aggregator sub-agent to the user. Detailed tables are based on the markdown already produced by the aggregator sub-agent (Read on demand).
- Steps 1–5 — delegate to sub-agents following review-respond §. Parallelization and the retry loop are orchestrated by the leader as defined in that skill.

Round-specific overrides:

- Progress display console output:
  - At triage start: `## Round {N} — Step 2: Triage`
  - At estimate start: `## Round {N} — Step 2.5: Estimate`
  - At fix / verify / update / commit start: `## Round {N} — Step 3: Review Respond (Fix & Verify)`
- Additional constraints for triage / estimate sub-agents:
  - Do not reference the previous round's review document.
  - The triage report must explicitly state the Will Fix count (including when zero).
  - When the estimate evaluates spread signal e (Will Fix originating from a FIXME), check whether the finding has its origin in a `FIXME:` / `TODO:` in the review text or in the target files.
- Round-loop control after triage:
  - 0 Will Fix findings: skip Steps 2.3–2.4 and proceed to Step 2.5 (Round End).
  - 1 or more Will Fix findings: proceed to the estimate phase.
- Round-loop control after estimate:
  - Both Maintain and Alternative are 0 (all Downgrade): skip the fix and build-verification phases; perform only the document update (review-respond § Step 5) and end the round.
  - 1 or more Maintain or Alternative findings: proceed to the fix phase (regular fix for Maintain, FIXME insertion for Alternative). If `--confirm` is enabled, display the estimate result and wait for user confirmation.

### 2.3 — Review Verification (review-resolve)

The orchestrator (you) directly takes on the "review verification leader" role of review-resolve. Procedure and templates follow `.claude/skills/review-resolve/SKILL.md`.

Input document: this round's file path

- Step 4 (Completion Report) — the orchestrator displays the `summary_line` received from the aggregator sub-agent to the user.
- Step 1 (Parse) · Step 2 (Verify each finding) · Step 3 (Verification report and reflection / events.jsonl write / render execution) — delegate to sub-agents following review-resolve § (parallelization rules also follow that skill).

Procedure:

1. Print to console: `## Round {N} — Step 4: Review Resolve`
2. Following review-resolve § procedure, launch the parsing Sub → verification Sub group (parallel) → aggregator Sub in order.
3. The orchestrator holds only the return value (`{events_path, summary_path, summary_line, resolved_count, feedback_count, unresolved_count}`) in context. Do not read verification bodies.

### 2.4 — Feedback Check and Re-fix Loop

Decide based on the return value from Step 2.3 (`feedback_count` from the aggregator sub-agent) whether feedback findings exist. There is no need to Read the review document body directly.

- `feedback_count == 0`: end the round (proceed to 2.5).
- `feedback_count > 0`: enter the re-fix loop (up to 3 iterations).

Re-fix loop (up to 3 iterations):

Each attempt runs review-respond's triage-to-aggregate flow once, then re-runs review-resolve's verify-to-aggregate at the end. Append a "Feedback finding priority" additional constraint to each sub-agent prompt.

1. Print `## Round {N} — Step 4.1: Feedback Triage (attempt {M}/3)`. Re-run review-respond § Step 1. Append to the triage prompt: `Prioritize triaging findings whose stage is "feedback" (current_meta.verification has Feedback details).`

2. Print `## Round {N} — Step 4.2: Feedback Estimate (attempt {M}/3)`. Re-run review-respond § Step 2. Append to the estimate prompt: `Estimate taking the Feedback content in current_meta.verification into account. Consider Downgrade if the cost inflates.` If every finding is Downgraded, skip step 3 and proceed to step 4.

3. Print `## Round {N} — Step 4.3: Feedback Fix (attempt {M}/3)`. Re-run review-respond § Steps 3–5. Append to the fix prompt: `Re-fix taking the Feedback content in current_meta.verification into account.`

4. Print `## Round {N} — Step 4.4: Feedback Verify (attempt {M}/3)`. Re-run review-resolve in the same manner as Step 2.3.

5. If feedback remains, return to step 1. If 3 iterations do not resolve it, end the round (any remaining 💬 Feedback annotations are counted as "unresolved" in 2.5).

6. If `--confirm-round` is enabled and unresolved items remain, wait for user confirmation before proceeding to the next round.

### 2.5 — Round End

Record the round's results. Each counter is obtained from the sub-agent return values (do not Read the review document body to count):

- Actionable findings count: triage Sub's `will_fix_count`.
- Maintain / Alternative / Downgrade counts: estimate aggregator Sub's `maintain_count` / `alternative_count` / `downgrade_count`.
- Fixed count: review-respond aggregator Sub's `fixed_count` (sum of Maintain regular fixes and Alternative FIXME insertions).
- Unresolved count: after the last attempt of Step 2.4, review-resolve aggregator Sub's `feedback_count` (Will Fix items not resolved within this round).
- Resolved count: review-resolve aggregator Sub's `resolved_count`.

Conditions to proceed to the next round: increment the round counter and return to Step 2.1 only if **all** of the following are satisfied:

1. The round counter is less than or equal to `--max-rounds`.
2. At least one line of source code was changed in this round.

If any of the above is not satisfied, proceed to final-report generation.

## Step 3 — Final Report (Delegated to Final-Report Aggregator Sub-Agent)

Final report path: `{base-path}/{branch-dir}/final-report.md`

Launch procedure:

1. Launch a sub-agent via the Agent tool. Example prompt:

```
Generate the final report from all rounds' review documents.

Input:
- Per-round review documents: Round 1 → {round1_doc_path}, Round 2 → {round2_doc_path}, ...
- Per-round statistics (reference info): Round 1: findings=N, will_fix=N, maintain=N, alternative=N, downgrade=N, fixed=N, wontfix=N, feedback_attempts=N, unresolved=N, code_changed=<bool>, ...
- Report template: {template_path}
- Output: {report_path}
- Language: the user's chat language

What to do:
1. Read the template markdown to grasp its structure (`<...>` placeholders, table structure).
2. Read each round's md and extract Triage / Estimate / Status / Verification values from `<!-- METADATA(id) --> 〜 <!-- /METADATA(id) -->` to obtain per-finding details (severity / location / summary / resolution / separate-PR recommendation status, etc.).
3. Fill in the template's statistics summary, full finding list, future recommendations, and review-document index, then Write to {report_path}.

Return value: {report_path}
```

2. The orchestrator holds only the return value (`{report_path}`) in context.

### Final Report Format

Template: `.claude/skills/review-rounds/templates/final-report.md` (the final-report aggregator Sub Reads it to grasp the skeleton; the leader fills `{template_path}` in the Sub prompt with this path).

## Step 4 — Completion Report

Report the final report path to the user and concisely communicate the key statistics (total findings, resolved count, unresolved count).
