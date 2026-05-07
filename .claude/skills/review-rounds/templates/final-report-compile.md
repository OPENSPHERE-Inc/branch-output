---
name: final-report-compile
description: Prompt for the final report aggregator sub-agent that generates the final report from all rounds' review documents in review-rounds Step 3
template_id: 4f8a2d1c-9b35-4e67-a2c1-8b5d3f9e7a16
---

Generate the final report from all rounds' review documents. Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Input:

- Each round's review document: `{{round_doc_paths}}` (e.g., `Round 1 → {round1_doc_path}, Round 2 → {round2_doc_path}, ...`)
- Each round's statistics (reference information): `{{round_stats}}` (e.g., `Round 1: findings=N, will_fix=N, maintain=N, alternative=N, downgrade=N, fixed=N, wontfix=N, feedback_attempts=N, unresolved=N, code_changed=<bool>, ...`)
- Report template: `{{template_path}}`
- Output path: `{{report_path}}`
- Language: `{{language}}`

What to do:

1. Read the template markdown to grasp the structure (`<...>` placeholders, table structure).
2. From each round's md `<!-- METADATA(id) --> ... <!-- /METADATA(id) -->`, extract Triage / Estimate / Status / Verification values to obtain per-finding details (severity / location / summary / response / whether a separate-PR recommendation is attached, etc.).
3. Fill the template's statistics summary, full findings list, future recommendations, and review document list, and Write to `{{report_path}}`.

Return value: `{report_path, template_id}`. Include `template_id` exactly as Read from this template's frontmatter.
