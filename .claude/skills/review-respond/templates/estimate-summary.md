---
name: estimate-summary
description: Prompt for the estimate-aggregator sub-agent that produces the estimate result summary in review-respond Step 2
template_id: 5c1e9b7a-3d48-4a96-b8e2-7f3c5a1d4b29
---

As the estimate-summary owner, Read `{{tmp_dir}}/triage.json`, all `{{tmp_dir}}/estimates/*.json`, and `{{document_path}}`, and Write the integrated summary to `{{tmp_dir}}/estimate-summary.md`. Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

## Input handling

- Each item in triage.json (both Will Fix and Won't Fix) is part of the output. Estimates exist only for Will Fix, so use triage.json as the primary axis and join `estimates/{id}.json` by id.
- Read the review document `{{document_path}}` and obtain severity / location / description for each finding (extracted from the heading and body around the METADATA marker). Compress description into a 1–2 sentence summary.

## `{{tmp_dir}}/estimate-summary.md` structure

1. Place a link line to the review document at the top: `Detail: [{basename}]({{document_path}})` (`{basename}` is the trailing file name of `{{document_path}}`).
2. Integrated table (columns): Finding ID / Severity / Location / Summary / Specialist / Triage / Cost / Future / Signals / Estimate Verdict (▶️ Maintain | 🔻 Downgrade | 🚧 Alternative) / Note.
   - Severity: Critical / Major / Minor / Info
   - Location: file:line (may shorten to basename:line when long)
   - Summary: 1–2 sentence summary of the description
   - Specialist: assignee from triage.json (`—` for Won't Fix)
   - Triage: 🔧 Will Fix / 🚫 Won't Fix
   - For a Won't Fix row, fill the four estimate columns (Cost / Future / Signals / Estimate Verdict) with `—`, and place a summary of the triage.json reason in the Note column.
   - Note: supplementary information such as "recommend separate PR" / FIXME insertion, or a Won't Fix reason summary.

## Return value

`{summary_path, summary_line (<=200 chars; e.g. "C-1 Maintain / M-2 Downgrade(separate PR) / M-3 Alternative"), maintain_count, downgrade_count, alternative_count, template_id}`. Include `template_id` (Read from this template's frontmatter) verbatim in the return value.
