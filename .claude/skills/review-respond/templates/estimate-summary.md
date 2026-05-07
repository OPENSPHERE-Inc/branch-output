---
name: estimate-summary
description: Prompt for the estimate-aggregator sub-agent that produces the estimate result table in review-respond Step 2
template_id: 5c1e9b7a-3d48-4a96-b8e2-7f3c5a1d4b29
---

As the estimate-summary owner, Read all `{{tmp_dir}}/estimates/*.json`, cross-reference them with `{{tmp_dir}}/triage.json`, and Write a markdown table to `{{tmp_dir}}/estimate-summary.md`. Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Table columns: Finding ID / Specialist / Cost / Future / Signals / Verdict (▶️ Maintain | 🔻 Downgrade | 🚧 Alternative) / Note (supplementary information such as "recommend separate PR" or FIXME insertion).

Return value: `{summary_path, summary_line (<=200 chars; e.g. "C-1 Maintain / M-2 Downgrade(separate PR) / M-3 Alternative"), maintain_count, downgrade_count, alternative_count, template_id}`. Include `template_id` (Read from this template's frontmatter) verbatim in the return value.
