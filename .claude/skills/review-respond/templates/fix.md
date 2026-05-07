---
name: fix
description: Prompt for the fix sub-agent that fixes assigned findings in review-respond Step 3
template_id: 2f8a1c5d-7b94-4e63-a1c8-5d3f9b2e7a14
---

Fix the assigned findings `{{ids}}` sequentially. Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Inputs (look up by id == "{finding-id}"):

- Review document `{{document_path}}` — Obtain description / location from around the METADATA marker.
- `{{tmp_dir}}/triage.json` — Obtain verdict / reason from the items array by matching id.
- `{{tmp_dir}}/estimates/{finding-id}.json` — verdict / cost / future / signals / rationale.

For each id:

1. Read the related source to grasp context.
2. Implement the fix (conform to the coding conventions in CLAUDE.md):
   - Maintain: A normal fix that follows the finding.
   - Alternative: Add a FIXME: comment only (no logic change). Use wording aligned with the direction stated in estimate.rationale.
3. Self-review: Re-read the changed locations, check for new issues introduced (regressions, thread safety, resource leaks, etc.), and fix any found before reporting.
4. Write to `{{tmp_dir}}/statuses/{finding-id}.json`.

Parallelization constraints (when handling multiple ids):

- Multiple ids that affect the same file are processed sequentially (to prevent write conflicts).
- ids that affect different files may be processed in parallel.

`{{tmp_dir}}/statuses/{finding-id}.json` format: `{id, specialist, description (concise description of the fix), memo_value}`

memo_value format:

- Maintain: `🟢 Fixed — {fix description}`
- Alternative: `🟢 Fixed — FIXME comment inserted at {file:line}` (description carries the same intent)

Return value: `{items: [{id, path}, ...], template_id}` (items covers all assigned ids). Include `template_id` (Read from this template's frontmatter) verbatim in the return value.
