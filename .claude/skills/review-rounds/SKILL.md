---
name: review-rounds
description: Automatically iterate parallel review, respond, and resolve across multiple rounds until no actionable findings remain
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*), Bash(git status:*), Bash(git branch:*), Bash(mkdir:*), Bash(cmake:*), Bash(make:*), Bash(pwsh:*), Bash(clang-format:*), Bash(cmake-format:*), Bash(.claude/scripts/rm-tmp.sh:*)
---

# Automatic Review Round Execution

You act as the **review round orchestrator**, automatically iterating a flow equivalent to parallel-review / review-respond / review-resolve across multiple rounds to comprehensively discover and fix significant issues. You do not take on the role of a reviewer or fix author yourself; everything is delegated to sub-agents. See "Sub-agent usage rules" for detailed responsibility allocation.

## Input

The user may optionally specify an output base path. When the argument is `$ARGUMENTS`, interpret it as the output base path (and options). When no output base path is specified, use the project root's `.claude/tmp/` as the default.

## Options

- `--confirm` (default OFF) — Wait for user confirmation immediately after estimate results are gathered (before proceeding to fix).
- `--confirm-round` (default OFF) — After review-resolve, if unresolved findings remain, wait for user confirmation before proceeding to the next round.
- `--commit` (default OFF) — Perform a git commit after each finding is fixed (the orchestrator performs an aggregated commit).
- `--max-rounds N` (default 5, range 1–10) — Change the maximum number of rounds for the outer loop.
- `--base {branch}` (default `main` or `master`) — Specify the base branch (passed to parallel-review).

## Review document file naming

- Format: `{base-path}/{branch-dir}/review-round{N}.md`
- Branch name retrieval: get the current branch name with `git branch --show-current`.
- The branch name is treated as a directory path — the entire branch name (including `/`) becomes the directory hierarchy.
- On re-runs on the same branch, append a sequential number `{branch-name}_1`, `{branch-name}_2`, ... to the suffix (smallest non-existing number).
  - Example: branch `feat/add-replay`, first run → `{base-path}/feat/add-replay/review-round1.md`, re-run → `{base-path}/feat/add-replay_1/review-round1.md`
- Default base-path: `.claude/tmp/`. Create the directory as needed.

## Review document language

Write the review document in the user's chat language.

## Sub-agent usage rules

- **For common prohibitions, see `.claude/rules/sub-agent.md`.**
- **The prompt body for each sub-agent is stored in an external template (`templates/*.md`, with `template_id` in the frontmatter).** When launching a sub-agent via the Agent tool, the orchestrator passes a launch prompt that says "Read the template and follow its instructions," with variable values and round-specific overrides filled in. The sub-agent includes `template_id` in its return value. The orchestrator verifies that the returned `template_id` matches the UUID specified in each Step (described below, hard-coded per Step), and relaunches the sub-agent if it does not match. The UUIDs of the sub-agents launched in Steps 2.1 / 2.2 / 2.3 are documented in the parallel-review / review-respond / review-resolve SKILLs.
- **Sub-agent nesting is prohibited** — when you yourself are launched as a sub-agent, you cannot launch further sub-agents from there.
- **Most work, including aggregation and compilation, is delegated to sub-agents** (one level of nesting is allowed):
  - Individual reviewers (Step 2.1) — launch cpp-sensei / qt-sensei / obs-sensei / network-sensei etc. in parallel for individual review. Each reviewer Writes findings to a file; return value is path and counts only.
  - Aggregator sub-agent (Step 2.1) — Reads each individual reviewer's output file and merges them into the review document (parallel-review § Step 3).
  - Analysis sub-agent (review-resolve, Step 2.3 / 2.4) — Reads the review document and returns the verification assignment (by_assignee) (review-resolve § Step 1, no file output).
  - Triage sub-agent (Step 2.2 / 2.4) — judging in a separate context to avoid bias, directly Reads the review document and performs finding extraction and judgment in a single stage (review-respond § Step 1).
  - Individual estimate (Step 2.2 / 2.4) — delegated in parallel to per-assignee specialist sub-agents; each Sub batch-estimates its assigned ids (review-respond § Step 2, read-only).
  - Estimate aggregator sub-agent (Step 2.2 / 2.4) — generates a summary of individual estimate results (review-respond § Step 2).
  - Individual fix (Step 2.2 / 2.4) — delegated to per-assignee specialist sub-agents; each Sub sequentially fixes its assigned ids (review-respond § Step 3).
  - Format & build verification sub-agent (Step 2.2 / 2.4) — runs clang-format / cmake-format + build once; on failure, identifies the specialist via code analysis (does not perform fixes; returns recommendation only).
  - Build-fix specialist sub-agent (Step 2.2 / 2.4) — fixes build errors as the specialist identified by the format & build verification Sub. After completion, the leader relaunches the format & build verification Sub (review-respond § Step 4).
  - Verification sub-agent (Step 2.3 / 2.4) — launched in parallel per specialist; batch-verifies the assigned findings (review-resolve § Step 2, read-only).
  - Aggregator sub-agent (Step 2.2 / 2.3 / 2.4) — generates events.jsonl from intermediate files and runs render-review.py (review-respond § Step 5 / review-resolve § Step 3).
  - Final report aggregator sub-agent (Step 3) — generates the final report from all rounds' review documents.
- **What the orchestrator (you) directly handles is limited to the following:**
  - Control between Steps and round loop judgment (including the format & build verification Sub ⇄ build-fix specialist Sub re-execution loop. Operational data files from each Sub may be Read; source code itself is not read.).
  - Sub-agent launch and aggregation of return values (lightweight counters, paths, and one-line summaries).
  - Final summary presentation to the user.
- **The orchestrator does not put review finding bodies or judgment bodies into context.** It holds only file paths and lightweight counters; details are handled by sub-agents.
- Each round's results are passed to the next Step / next round **only through the review document**. Intermediate data between sub-agents is self-contained within a Step (within the same Instruction) and must not persist across Steps.
- **Launch aggregator / compilation / analysis / format & build verification sub-agents via `subagent_type="review-helper"`** (`model: sonnet` is already specified in review-helper's agent definition). Targets: parallel-review § Step 3 aggregator Sub / review-respond § Step 2 estimate aggregator Sub / review-respond § Step 4 format & build verification Sub / review-respond § Step 5 aggregator Sub / review-resolve § Step 1 analysis Sub / review-resolve § Step 3 aggregator Sub / review-rounds § Step 3 final report aggregator Sub. For all other sub-agents, specify a specialist (e.g., cpp-sensei) via `subagent_type`, or use `subagent_type="general-purpose"` as for triage. Do not specify `model="..."` from the SKILL (the model follows each agent definition's frontmatter).

For the launch prompt completeness convention, see `.claude/rules/sub-agent.md` § Launch prompt completeness.

## Flow overview

```
Round 1 start
  ├─ 2.1 parallel-review (orchestrator)
  │     [specialist reviewers] launched in parallel → each Writes to reviews/{name}.md
  │     [aggregator Sub] Reads each reviews/{name}.md → generates round1.md (no triage yet)
  ├─ 2.2 review-respond (orchestrator)
  │     [triage Sub] Reads round1.md → generates triage.json (includes by_assignee)
  │     ↳ if Will Fix is 0, end the round
  │     [estimate Subs (per-assignee, parallel)] batch-estimate assigned ids → estimates/{id}.json
  │     [estimate aggregator Sub] estimates/*.json → estimate-summary.md
  │     ↳ if both Maintain and Alternative are 0, skip fix and build verification
  │     [fix Subs (per-assignee, parallel)] fix Maintain, attach FIXME for Alternative → statuses/{id}.json
  │     [format & build verification Sub] ⇄ [build-fix specialist Sub] loop (max 5, leader-controlled)
  │     [aggregator Sub] triage.json + estimates/*.json + statuses/*.json → events.jsonl → render-review.py
  ├─ 2.3 review-resolve
  │     [analysis Sub] Reads round1.md → by_assignee return value (no file output)
  │     [verification Subs] per-specialist parallel, batch-verifies assigned findings → verifications/{id}.json
  │     [aggregator Sub] verifications/*.json → events.jsonl → render-review.py
  ├─ 2.4 feedback re-fix loop (max 3)
  │     [triage Sub] → [estimate Subs (per-assignee)] → [fix Subs (per-assignee)]
  │       → [format & build verification Sub] ⇄ [build-fix specialist Sub] loop
  │       → [aggregator Sub] → [verification Subs] → [aggregator Sub]
  └─ 2.5 round end → judge condition for proceeding to the next round
Round 2 start (do not pass the previous round's review document)
  └─ ...
Final step
  └─ [final report aggregator Sub] all round{N}.md + template → final-report.md
```

## Step 1 — Initialization

1. Verify the output directory exists; if not, create it.
2. Get the current branch name.
3. Parse options.
4. Set the round counter to 1.

## Step 2 — Round loop

While the round counter is at most `--max-rounds`, repeat the following.

### 2.1 — Run review (parallel-review)

The orchestrator (you) directly takes on the "review leader" role of parallel-review. Follow the procedure, templates, and format in `.claude/skills/parallel-review/SKILL.md`.

Procedure:

1. Display in console: `## Round {N} — Step 1: Parallel Review`
2. Per parallel-review § Step 1, prepare the working directory and diff file, and launch the scope analysis sub-agent. Hold only the return values (`line_count` / `recommended_reviewers`) in context.
3. Launch each `name` in `recommended_reviewers` in parallel via `Agent(subagent_type=name, prompt=...)` (the agent definition's persona and viewpoint are auto-loaded). Each reviewer Writes findings to `{tmp_dir}/reviews/{name}.md`; return value is `{path, severity counts}` only.
4. Per parallel-review § Step 3, launch the aggregator sub-agent to generate the review document (output path: {this round's file path}, language: user's chat language). Hold only the aggregator sub-agent's return value (`{doc_path, findings_total, severity_counts}`) in context.
5. Per parallel-review § Step 4, delete `{tmp_dir}`.

Round-specific overrides (apply after following the template's instructions):

- Do not pass the previous round's review document to reviewers (bias avoidance).
- Do not perform deduplication against the previous round.
- Convergence-induction prevention:
  - **The following must NEVER be included in the reviewer's prompt:**
    - Past round finding counts, count trends, or trend information such as "appears to be converging."
    - Past round finding IDs (`C-1`, `M-1`, etc.).
    - Statistics such as Fixed / Won't Fix counts from past rounds.
  - It is prohibited to omit parts of the reviewer prompt template or to add instructions in an attempt to adjust the finding count.
  - The review orchestrator itself is prohibited from adding findings other than those submitted by the reviewers.

### 2.2 — Review response (review-respond)

The orchestrator (you) directly takes on the "review respond leader" role of review-respond. Follow the procedure and templates in `.claude/skills/review-respond/SKILL.md`.

Input document: {this round's file path}

- Step 6 (summary) — the orchestrator displays the `summary_line` received from the aggregator sub-agent to the user. Read the markdown already generated by the aggregator sub-agent for the detail table as needed.
- Steps 1–5 — delegate to sub-agents per the review-respond § instructions. Parallelization and re-execution loop orchestration are handled by the leader per that SKILL.

Round-specific overrides (apply after following the template's instructions):

- Console output timing for progress display:
  - At triage start: `## Round {N} — Step 2: Triage`
  - At estimate start: `## Round {N} — Step 2.5: Estimate`
  - At fix / verify / update / commit start: `## Round {N} — Step 3: Review Respond (Fix & Verify)`
- Additional constraints for the triage / estimate sub-agents:
  - Do not reference the previous round's review document.
  - State the Will Fix count explicitly in the triage report (also state explicitly when 0).
  - When determining the diffusion signal e (Will Fix originating from FIXME) during estimate, verify whether the finding originates from a `FIXME:` / `TODO:` in the review body or target file.
- Round loop control after triage:
  - Will Fix == 0: skip Steps 2.3–2.4 and proceed to Step 2.5 (round end).
  - Will Fix >= 1: proceed to the estimate phase.
- Round loop control after estimate:
  - Both Maintain and Alternative are 0 (all Downgrade): skip the fix and build verification phase, perform only the document update (review-respond § Step 5), and end the round.
  - Maintain or Alternative >= 1: proceed to the fix phase (Maintain via normal fix, Alternative via FIXME attachment). When `--confirm` is enabled, display the estimate result and wait for user confirmation.

### 2.3 — Review verification (review-resolve)

The orchestrator (you) directly takes on the "review verification leader" role of review-resolve. Follow the procedure and templates in `.claude/skills/review-resolve/SKILL.md`.

Input document: {this round's file path}

- Step 4 (completion report) — the orchestrator displays the `summary_line` received from the aggregator sub-agent to the user.
- Step 1 (analysis) / Step 2 (per-finding verification) / Step 3 (verification report and reflection / events.jsonl write / render execution) — delegate to sub-agents per the review-resolve § instructions (parallelization rules also follow that SKILL).

Procedure:

1. Display in console: `## Round {N} — Step 4: Review Resolve`
2. Per the review-resolve § procedure, launch in order: analysis Sub → verification Subs (parallel) → aggregator Sub.
3. The orchestrator holds only the return values (`{events_path, summary_path, summary_line, resolved_count, feedback_count, unresolved_count}`) in context. Do not read the verification body.

### 2.4 — Feedback confirmation and re-fix loop

From the Step 2.3 return value (`feedback_count` received from the aggregator sub-agent), determine whether there are findings that "require feedback." There is no need to Read the review document body directly.

- `feedback_count == 0`: end the round (proceed to 2.5).
- `feedback_count > 0`: enter the re-fix loop (max 3).

Re-fix loop (max 3):

Each attempt runs the review-respond triage-through-aggregator flow once, then re-runs the review-resolve verification-through-aggregator at the end. In each sub-agent's launch prompt, add an "Feedback finding priority" additional constraint to the "Round-specific overrides" section.

1. Display `## Round {N} — Step 4.1: Feedback Triage (attempt {M}/3)`. Re-run review-respond § Step 1. Add to "Round-specific overrides" of the triage launch prompt: `Triage findings whose stage is "feedback" with priority (current_meta.verification has Feedback details).`

2. Display `## Round {N} — Step 4.2: Feedback Estimate (attempt {M}/3)`. Re-run review-respond § Step 2. Add to "Round-specific overrides" of the estimate launch prompt: `Estimate based on the Feedback content in current_meta.verification. Consider Downgrade if cost grows.` If all are Downgrade, skip step 3 and proceed to step 4.

3. Display `## Round {N} — Step 4.3: Feedback Fix (attempt {M}/3)`. Re-run review-respond § Steps 3–5. Add to "Round-specific overrides" of the fix launch prompt: `Re-fix based on the Feedback content in current_meta.verification.`

4. Display `## Round {N} — Step 4.4: Feedback Verify (attempt {M}/3)`. Re-run review-resolve in the same manner as Step 2.3.

5. If feedback remains, return to 1. If not resolved within 3 attempts, end the round (remaining 💬 Feedback are counted as "unresolved" in 2.5).

6. When `--confirm-round` is enabled and unresolved findings remain, wait for user confirmation before proceeding to the next round.

### 2.5 — Round end

Record the round's results. Each counter is obtained from sub-agent return values (do not Read the review document body to count):

- Findings requiring action: triage Sub's `will_fix_count`
- Maintain / Alternative / Downgrade counts: estimate aggregator Sub's `maintain_count` / `alternative_count` / `downgrade_count`
- Fixed count: review-respond aggregator Sub's `fixed_count` (sum of Maintain normal fixes + Alternative FIXME attachments)
- Unresolved count: review-resolve aggregator Sub's `feedback_count` after the final attempt of Step 2.4 (Will Fix but not resolved in this round)
- Resolved count: review-resolve aggregator Sub's `resolved_count`

Condition for proceeding to the next round: only when **all** of the following are met, increment the round counter and return to Step 2.1:

1. The round counter is at most `--max-rounds`.
2. At least one line of source code has changed in this round.

If not met, proceed to final report generation.

## Step 3 — Final report (delegate to the final report aggregator sub-agent)

Final report path: `{base-path}/{branch-dir}/final-report.md`

Launch procedure:

1. Launch the sub-agent via `Agent(subagent_type="review-helper", prompt=...)`. The task-specific instructions are stored in the external template `templates/final-report-compile.md`. Example launch prompt:

```
As your first action, you MUST Read `.claude/skills/review-rounds/templates/final-report-compile.md`. Do not perform any other judgment, action, or tool call before the Read completes. After reading, follow its instructions.

Variables (substitute into the template's {{...}} placeholders):
- round_doc_paths: Round 1 → {round1_doc_path}, Round 2 → {round2_doc_path}, ...
- round_stats: Round 1: findings=N, will_fix=N, maintain=N, alternative=N, downgrade=N, fixed=N, wontfix=N, feedback_attempts=N, unresolved=N, code_changed=<bool>, ...
- template_path: {template_path}
- report_path: {report_path}
- language: user's chat language

Round-specific overrides (apply after following the template's instructions):
- (none)

Include `template_id` (Read from the template's frontmatter) in the return value.
```

2. The orchestrator receives the return value (`{report_path, template_id}`). Verify that `template_id` matches `4f8a2d1c-9b35-4e67-a2c1-8b5d3f9e7a16`. If it does not match, relaunch the sub-agent. Hold only `report_path` in context.

### Final report format

Template: `.claude/skills/review-rounds/templates/final-report.md` (the final report aggregator Sub Reads it to grasp the skeleton. The leader fills this path into the Sub prompt's `{template_path}`).

## Step 4 — Completion report

Report the final report path to the user, and concisely convey key statistics (total findings, resolved count, unresolved count).
