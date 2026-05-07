---
name: compile
description: Prompt for the aggregator sub-agent that compiles intermediate files and reflects them into the markdown via events.jsonl in review-respond Step 5
template_id: 3b7f1c5d-8a29-4e63-b1c8-9d3a7f5e2b41
---

As the review-respond compile owner, aggregate the intermediate files and reflect them into the markdown via events.jsonl. Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Inputs:

- triage: `{{tmp_dir}}/triage.json`
- estimate: `{{tmp_dir}}/estimates/`
- status: `{{tmp_dir}}/statuses/`
- Target markdown: `{{document_path}}`

Outputs:

- events.jsonl: `{{events_path}}` (`{basename}.events.jsonl` in the same directory as `{{document_path}}`)
- The updated `{{document_path}}`

What to do:

1. Read triage.json / estimates/*.json / statuses/*.json and collect each item's memo_value as an event for the corresponding field (triage / estimate / status).
2. Write JSONL to `{{events_path}}`, one event per line. Format: `{"id":"...","field":"triage|estimate|status","value":"..."}`
3. Run `python .claude/scripts/render-review.py {{document_path}} {{events_path}} {{document_path}}`.
4. Remove events.jsonl with `.claude/scripts/rm-tmp.sh {{events_path}}`.

Return value: `{events_path, fixed_count (number of files in statuses = Maintain fixes + Alternative FIXME insertions), code_changed (true if at least one status; false if all Downgrade), summary_line (<=200 chars; e.g. "3 fixed (2 Maintain + 1 Alternative), 1 Downgrade, 1 Wontfix"), template_id}`. Include `template_id` (Read from this template's frontmatter) verbatim in the return value.
