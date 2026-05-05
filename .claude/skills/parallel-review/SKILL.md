---
name: parallel-review
description: Launch parallel code review with multiple specialist reviewers
allowed-tools: Agent, Read, Glob, Grep, Bash(.claude/scripts/fetch-diff.sh:*), Bash(.claude/scripts/rm-tmp.sh:*), Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(mkdir:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*)
---

# Parallel Code Review

You are the **review leader**. Orchestrate parallel code reviews using specialist reviewers and consolidate each reviewer's findings into a single report.

The review leader does not act as a reviewer. The role is strictly to orchestrate, aggregate, and judge the overall review. All reviewer work is delegated to sub-agents.

## Round Number

If the arguments include a round number (e.g., `Round 1`, `Round 2`), reflect it in the report title.

## Input

The user specifies one or more of the following as the review target:
- File paths or glob patterns
- A git diff range (e.g., `HEAD~3..HEAD`, branch name, PR)
- A description of the area to review

If the argument is `$ARGUMENTS`, interpret it as the review target specification (including round number and options).

## Options

- `--base {branch}` — Specifies the base branch. Defaults to `main` or `master`.

### Default Review Targets

If the user does not explicitly specify a review target, use the following as the default targets:

1. Commits unique to the current branch — all commits since the divergence point from the base branch (equivalent to `git log {base}..HEAD`).
2. Working tree changes — both staged (`git diff --cached`) and unstaged (`git diff`) changes.

If a base branch is not specified via `--base`, use `main` or `master` if either exists on the remote (prefer `main` when both exist).

## Sub-Agent Common Instructions

For common prohibitions, see `.claude/rules/sub-agent.md`. The leader appends this file's content to each sub-agent prompt before passing it to the Agent tool.

## Step 1 — Identify Review Scope and Fetch Diff

The leader (you) does not Read the diff content. Diff parsing, line counting, and reviewer-candidate selection are delegated to a scope-analysis sub-agent. The leader receives only the return value (line count + candidate list + summary). The reviewer perspective string (`{perspective}`) is also returned by the scope-analysis sub-agent.

1. Based on the user's input, identify the review target (base branch, target paths, etc.) and any user-explicitly-requested reviewers (if any).
2. Create the working directory:
   - Temporary directory: `.claude/tmp/parallel-review-{timestamp}/`
   - Reviewer output sub-directory: `{tmp_dir}/reviews/`
   - Create both with `mkdir -p`.
3. Fetch the diff via script:
   - Output file path: `{tmp_dir}/diff.txt`
   - Run:
     ```
     .claude/scripts/fetch-diff.sh {base} {tmp_dir}/diff.txt
     ```
4. Launch the scope-analysis sub-agent to parse the diff. Example prompt:

```
You are responsible for review-scope analysis. Read {tmp_dir}/diff.txt, count lines, and select required reviewer candidates.

User-explicitly-requested reviewers: {user_requested} (may be an empty array)

Reviewer candidate mapping (kind / selection criteria):

- cpp-sensei (required): always include
- qt-sensei (required): always include
- obs-sensei (required): always include
- network-sensei (required): always include
- av-sensei (optional): include when changes to encoder configuration / media pipelines / A/V quality / streaming output configuration are present in the diff
- devops-sensei (optional): include when changes to CMakeLists.txt / *.cmake / .github/workflows / build.ps1 / .clang-format / .cmake-format / Inno Setup / packaging configuration are present in the diff
- python-sensei (optional): include when .py file changes are present in the diff
- lua-sensei (optional): include when .lua file changes are present in the diff

Each reviewer's specialty and perspective is defined in its agent definition (.claude/agents/{name}.md). This mapping shows only the selection criteria.

What to do:
1. Add all required reviewers to recommended (reason: "required").
2. Add optional reviewers only when the selection criterion is met (record the matched extension/path in reason).
3. Add user_requested reviewers that are not yet included (reason: "user explicitly requested").
4. line_count = total of +/- lines in the diff.

Return value: {line_count, recommended_reviewers: [{name, reason}], extension_summary (e.g., ".cpp(12), .hpp(5)"), rationale (1–2 sentence basis for optional additions)}
```

5. The leader uses the returned `recommended_reviewers` directly as the final reviewer list, and passes each element's `name` to `subagent_type` in Step 2.
6. If `line_count == 0`, generate an empty review document at `{output_path}` and proceed to Step 4 (skip Steps 2 and 3).

## Step 2 — Launch Parallel Reviewers

Launch all selected reviewers simultaneously via the Agent tool. Each reviewer does not return findings via stdout but writes them to a dedicated file. The review leader (you) does not load the output body into context (the aggregator sub-agent reads it later).

### Reviewer Output File

- One file per reviewer: `{tmp_dir}/reviews/{reviewer-name}.md`
- Content: only the numbered list of findings (no greetings or overall summary)
- Format: numbered list of `[Severity] file_path:line — Description of the issue and why it matters.`

### Agent Prompt Template

When launching the Agent tool, specify `subagent_type={name}` to load the persona and perspective from the agent definition (.claude/agents/{name}.md). Do not include persona / perspective in the prompt body; include only task-specific instructions. Fill in `{targets}`, `{base}`, `{diff_path}`, `{output_path}` and pass the following (omitting parts of the template or appending extra instructions is prohibited).

```
Read {diff_path} and conduct a code review.

Target: {targets} (base: {base})

Rules:
- Tool usage is limited to Read / Glob / Grep / Bash(grep/ls/find). Do not re-run git diff/log/show (the diff is already aggregated in {diff_path}). Use Read for surrounding code.
- Severity labels: Critical (fatal / must fix) / Major (medium risk / should fix) / Minor (advisory) / Info (informational).
- Follow .claude/rules/review.md (auto-loaded).

Output:
- Write only the numbered list in `[Severity] file_path:line — Description of the issue and why it matters.` format to {output_path} (no preamble or epilogue).
- Return value: {"path": "{output_path}", "critical": <int>, "major": <int>, "minor": <int>, "info": <int>}
```

## Step 3 — Report Integration (Delegated to Aggregator Sub-Agent)

After all reviewers have completed, launch the aggregator sub-agent to consolidate the report.
The review leader does not perform aggregation (Read each reviewer file, deduplicate, sort, Write the final artifact) and does not load reviewer output bodies into context.

### Aggregator Sub-Agent Prompt Template

```
You are responsible for review report aggregation. Consolidate the individual reviews under {tmp_dir}/reviews/ into a single final report. Do not perform triage (that is review-respond's responsibility).

Input: {reviewer_paths_list}
Output file: {final_doc_path}
Round number: {round_num_or_omitted}
Review target: {targets_description}
Reviewer list: {reviewer_names_csv}

Aggregation procedure:
1. Read each reviewer file.
2. Deduplicate — when multiple reviewers point out the same location with the same intent, merge them into one entry and record the originating reviewers.
3. Group by severity (Critical → Major → Minor → Info).
4. Within each group, assign finding-ids (C-1, C-2, ..., M-1, ..., m-1, ..., i-1).
5. Read `.claude/skills/parallel-review/templates/review-doc.md` to grasp the template skeleton, then Write to {final_doc_path} following the format rules in this SKILL § Format Rules.

Return value: {doc_path, findings_total, severity_counts: {critical, major, minor, info}, duplicates_merged}
```

### Final Report Format

Template: `.claude/skills/parallel-review/templates/review-doc.md` (the aggregator sub-agent Reads it to grasp the skeleton).

### Format Rules

- Each finding is its own subsection with a `### {finding-id} — `{location}`` heading.
- List metadata (Reviewers) as bullets per finding, then write the finding under a bold "Finding:" label.
- Place metadata insertion markers `<!-- METADATA({finding-id}) -->` and `<!-- /METADATA({finding-id}) -->` after the finding body and before the `---` separator, surrounded by blank lines. **Output the markers empty** (later stages mechanically inject metadata).
- Separate findings with a `---` horizontal rule. **Do not output Status lines** (out of scope for this skill).
- Severity sections (`## Critical` / `## Major` / `## Minor` / `## Info`) with no findings must **not be omitted**: emit the heading and write `No findings` in the body.

## Step 4 — Clean Up Temporary Files

After the aggregator sub-agent finishes writing the final report, delete the entire working directory created in Step 1 (including `diff.txt` and the reviewer files under `reviews/`).

```bash
.claude/scripts/rm-tmp.sh {tmp_dir}
```

The final report is written outside `{tmp_dir}` so it is not deleted.
