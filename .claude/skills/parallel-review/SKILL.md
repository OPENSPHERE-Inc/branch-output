---
name: parallel-review
description: Launch parallel code review with multiple specialist reviewers
allowed-tools: Agent, Read, Glob, Grep, Bash(.claude/scripts/fetch-diff.sh:*), Bash(.claude/scripts/rm-tmp.sh:*), Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(mkdir:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*)
---

# Parallel Code Review

As the **review leader**, you orchestrate a parallel code review using specialist reviewers and consolidate each reviewer's findings into a single report.

The review leader does not act as a reviewer; instead, the leader orchestrates the overall review and performs aggregation and judgment. All reviewer roles are delegated to sub-agents.

## Round Number

If the arguments include a round number (e.g., `Round 1`, `Round 2`), reflect it in the report title.

## Input

The user specifies one or more of the following as the review target:
- File path or glob pattern
- git diff range (e.g., `HEAD~3..HEAD`, branch name, PR)
- Description of the area to review

If the argument is `$ARGUMENTS`, interpret it as the review target specification (including round number and options).

## Options

- `--base {branch}` — Specify the base branch. Defaults to `main` or `master`.

### Default Review Target

If the user does not explicitly specify a review target, use the following as the default:

1. Commits unique to the current branch — all commits since the divergence from the base branch (equivalent to `git log {base}..HEAD`).
2. Working tree changes — staged (`git diff --cached`) and unstaged (`git diff`) changes.

If no base branch is specified via `--base`, use whichever of `main` or `master` exists on the remote (prefer `main` if both exist).

## Common Sub-Agent Instructions

For common prohibitions, see `.claude/rules/sub-agent.md`. The prompt body for each sub-agent is stored in an external template under `templates/*.md` (each template has a `template_id` in its frontmatter). When launching via the Agent tool, the leader passes a launch prompt that instructs the sub-agent to "Read the template and follow its instructions," with the variable values substituted in. The sub-agent must include `template_id` in its return value. The leader verifies that the returned `template_id` matches the UUID specified at each step (hardcoded per step, see below); if it does not match, the leader re-launches that sub-agent.

For launch-prompt-completeness rules, see `.claude/rules/sub-agent.md` § Launch Prompt Completeness.

## Step 1 — Identify Review Scope and Fetch Diff

The leader (you) does not Read the diff content. Diff analysis, line counting, and selection of required reviewer candidates are delegated to the scope-analysis sub-agent; the leader receives only the return value (line count + candidate list + summary).

1. Based on the user's input, identify the review target (base branch, target paths, etc.) and any explicitly requested reviewers (if any).
2. Create working directories:
   - Temporary directory: `.claude/tmp/parallel-review-{timestamp}/`
   - Reviewer output subdirectory: `{tmp_dir}/reviews/`
   - Create both with `mkdir -p`.
3. Fetch diff information via script:
   - Output file path: `{tmp_dir}/diff.txt`
   - Run:
     ```
     .claude/scripts/fetch-diff.sh {base} {tmp_dir}/diff.txt
     ```
4. Launch the scope-analysis sub-agent to analyze the diff. Example launch prompt:

```
As your first action, you MUST Read `.claude/skills/parallel-review/templates/scope-analysis.md`. Do not perform any other judgment, action, or tool call before the Read completes. After reading, follow its instructions.

Variables (substitute into the template's {{...}} placeholders):
- tmp_dir: {tmp_dir}
- user_requested: {user_requested}

Round-specific overrides (apply after following the template's instructions):
- (none)

Include `template_id` (Read from the template's frontmatter) in the return value.
```

5. Receive the return value (`{line_count, recommended_reviewers, extension_summary, rationale, template_id}`) from the sub-agent.
6. Verify that `template_id` matches `b3e2f1a7-9c84-4d56-8e3b-7f1a4c9d2e85`. If it does not match, re-launch the sub-agent.
7. Adopt `recommended_reviewers` as-is as the final reviewer list, and pass each element's `name` to `subagent_type` in Step 2.
8. If `line_count == 0`, generate an empty review document at `{output_path}` and proceed directly to Step 4.

## Step 2 — Launch Parallel Reviewers

Launch all selected reviewers concurrently via the Agent tool. Each reviewer must not return findings to stdout; instead, they Write to a dedicated file. The review leader (you) must not load reviewer output bodies into context (the aggregator sub-agent reads them in a later step).

### Reviewer Output Files

- One file per reviewer: `{tmp_dir}/reviews/{reviewer-name}.md`
- Content is only the "numbered list of findings" (no greetings or overall summaries before or after)
- Format: numbered list of `[severity] file_path:line — Description of the issue and its importance.`

### Agent Launch Prompt

When launching via the Agent tool, specify `subagent_type={name}` to load the persona and perspective from the agent definition (`.claude/agents/{name}.md`). Do not include the persona / perspective in the launch prompt. Task-specific instructions are stored in the `templates/reviewer.md` external template.

```
As your first action, you MUST Read `.claude/skills/parallel-review/templates/reviewer.md`. Do not perform any other judgment, action, or tool call before the Read completes. After reading, follow its instructions.

Variables (substitute into the template's {{...}} placeholders):
- targets: {targets}
- base: {base}
- diff_path: {diff_path}
- output_path: {output_path}

Round-specific overrides (apply after following the template's instructions):
- (none)

Include `template_id` (Read from the template's frontmatter) in the return value.
```

Receive the return value (`{path, critical, major, minor, info, template_id}`) from each reviewer. Verify that `template_id` matches `4d8c2e5b-1f73-4a96-b2e8-9c1d3a7f4b62`. If it does not match, re-launch that reviewer.

## Step 3 — Consolidate the Report (Delegate to Aggregator Sub-Agent)

After all reviewers complete, launch the aggregator sub-agent and delegate report consolidation to it.
The review leader does not perform aggregation work (Reading each reviewer file, deduplicating, sorting, Writing the deliverable) and does not load reviewer output bodies into context.

When launching via the Agent tool, specify `model="sonnet"`.

### Aggregator Sub-Agent Launch Prompt

Task-specific instructions are stored in the `templates/aggregator.md` external template.

```
As your first action, you MUST Read `.claude/skills/parallel-review/templates/aggregator.md`. Do not perform any other judgment, action, or tool call before the Read completes. After reading, follow its instructions.

Variables (substitute into the template's {{...}} placeholders):
- tmp_dir: {tmp_dir}
- reviewer_paths_list: {reviewer_paths_list}
- final_doc_path: {final_doc_path}
- round_num_or_omitted: {round_num_or_omitted}
- targets_description: {targets_description}
- reviewer_names_csv: {reviewer_names_csv}

Round-specific overrides (apply after following the template's instructions):
- (none)

Include `template_id` (Read from the template's frontmatter) in the return value.
```

Receive the return value (`{doc_path, findings_total, severity_counts, duplicates_merged, template_id}`) from the aggregator sub-agent. Verify that `template_id` matches `7a5f8c1d-3e92-4b67-9c4a-2d8e1f7b3c54`. If it does not match, re-launch the sub-agent.

### Final Report Format

Template: `.claude/skills/parallel-review/templates/review-doc.md` (the aggregator Sub Reads this to grasp the skeleton)

### Format Rules

- Each finding is an independent subsection with the heading `### {finding-id} — `{location}``.
- For each finding, list metadata (reviewers) as bullets, and below that write the "Finding" with a bold label.
- After the finding body and before the `---` separator, place the metadata insertion markers `<!-- METADATA({finding-id}) -->` and `<!-- /METADATA({finding-id}) -->`, separated from surrounding content by blank lines. **Output the space between the markers as empty** (a later step inserts metadata mechanically).
- Separate findings with a `---` horizontal rule. **Do not output a Status line** (it is outside this skill's responsibility).
- For severity sections with no applicable findings (`## Critical` / `## Major` / `## Minor` / `## Info`), **do not omit the heading**; output the heading and write `No findings` in the body.

## Step 4 — Clean Up Temporary Files

After the aggregator sub-agent completes Writing the final report, delete the entire working directory created in Step 1 (including `diff.txt` and the reviewer files under `reviews/`).

```bash
.claude/scripts/rm-tmp.sh {tmp_dir}
```
