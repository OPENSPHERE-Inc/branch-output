---
name: final-report-compile
description: Prompt for the final report aggregator sub-agent that generates the final report from all rounds' review documents in review-rounds Step 3
template_id: 4f8a2d1c-9b35-4e67-a2c1-8b5d3f9e7a16
---

<!--
This file is kept in sync with .claude/skills/review-rounds/templates/final-report-compile.md.
-->

Generate the final report from all rounds' review documents. Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Input:

- Each round's review document: `{{round_doc_paths}}` (e.g., `Round 1 → {round1_doc_path}, Round 2 → {round2_doc_path}, ...`)
- Each round's statistics (reference information): `{{round_stats}}` (e.g., `Round 1: findings=N, will_fix=N, maintain=N, alternative=N, downgrade=N, fixed=N, wontfix=N, feedback_attempts=N, unresolved=N, code_changed=<bool>, ...`)
- Report template: `{{template_path}}`
- Output path: `{{report_path}}`
- Language: `{{language}}`

What to do:

1. Read the template markdown to grasp the structure (`<...>` placeholders, table structure, and the subsection examples in the future-recommendations section).
2. From each round's md `<!-- METADATA(id) --> ... <!-- /METADATA(id) -->`, extract Triage / Estimate / Status / Verification values to obtain per-finding details (severity / location / summary / response / whether a separate-PR recommendation is attached, etc.).
3. Fill the template's statistics summary, full findings list, future recommendations, and review document list, and Write to `{{report_path}}`.
   - Aggregation rules for the "Recommended future actions" section:
     - Candidates: among Triage: 🚫 Won't Fix / Estimate: 🔻 Downgrade / Estimate: 🚧 Alternative, findings whose reason field explicitly states a separate-PR recommendation.
     - Exclusion: among the candidates, exclude any whose same-location, same-content counterpart was resolved as Status: 🟢 Fixed in a later round (already fixed, so no need to keep on the roadmap). Identity is judged by matching `file:line` and the finding summary. When the judgment is difficult, do not exclude; instead, append a note to the recommendation reason indicating that the judgment is deferred.
     - Format: produce one subsection per finding following the template's example. The heading uses the form `### R{source-round-number}-{source-ID} — `file:line`` (always prefix with the round number to avoid ID collisions across rounds). Inside the subsection, list severity / source round / source ID / source reviewer / decision as bullets, followed by the `**Finding:**` label and a **complete, unabbreviated transcription** of the finding body from the source review document (the range from immediately below `### {id} — ...` up to just before `---`, excluding the `<!-- METADATA(id) --> ... <!-- /METADATA(id) -->` block). Separate subsections with `---`.

Return value: `{report_path, template_id}`. Include `template_id` exactly as Read from this template's frontmatter.
