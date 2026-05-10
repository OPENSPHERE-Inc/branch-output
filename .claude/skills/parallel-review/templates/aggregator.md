---
name: aggregator
description: Prompt for the aggregator sub-agent in parallel-review Step 3, which consolidates reviewer output into the final report
template_id: 7a5f8c1d-3e92-4b67-9c4a-2d8e1f7b3c54
---

As the review-report aggregation owner, consolidate the individual reviews under `{{tmp_dir}}/reviews/` into a single final report. Do not perform triage (that is review-respond's responsibility). Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Input: `{{reviewer_paths_list}}`
Output file: `{{final_doc_path}}`
Round number: `{{round_num_or_omitted}}`
Review target: `{{targets_description}}`
Reviewer list: `{{reviewer_names_csv}}`

Consolidation procedure:

1. Read each reviewer file.
2. Deduplicate — merge findings at the same location with the same intent into a single entry, listing the originating reviewers together.
3. Group by severity (Critical → Major → Minor → Info).
4. Within each group, assign finding-ids (Critical: C-1, C-2, ...; Major: M-1, M-2, ...; Minor: mi-1, mi-2, ...; Info: I-1, I-2, ...).
5. Read `.claude/skills/parallel-review/templates/review-doc.md` to grasp the skeleton, and Write to `{{final_doc_path}}` following the format rules below.

Format rules:

- Each finding is an independent subsection with the heading `### {finding-id} — `{location}``.
- For each finding, list metadata (reviewers) as bullets, and below that write the "Finding" with a bold label.
- After the finding body and before the `---` separator, place the metadata insertion markers `<!-- METADATA({finding-id}) -->` and `<!-- /METADATA({finding-id}) -->`, separated from surrounding content by blank lines. Output the space between the markers as empty (a later step inserts metadata mechanically).
- Separate findings with a `---` horizontal rule. Do not output a Status line (it is outside this skill's responsibility).
- For severity sections with no applicable findings (`## Critical` / `## Major` / `## Minor` / `## Info`), do not omit the heading; output the heading and write `No findings` in the body.

Return value: `{doc_path, findings_total, severity_counts: {critical, major, minor, info}, duplicates_merged, template_id}`. Include `template_id` exactly as Read from this template's frontmatter.
