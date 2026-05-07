---
name: compile
description: Prompt for the aggregator sub-agent in review-resolve Step 3 that generates the verification report and events.jsonl from intermediate files and reflects them in the markdown
template_id: 1c5e8b2f-7d34-4a96-b8c1-5e9a3f7d2c84
---

As the aggregator for review verification, generate the verification report and events.jsonl from the intermediate files and reflect them in the markdown. Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Input:

- `{{tmp_dir}}/verifications/` — verification result for each finding (includes severity / trailing_field / feedback_detail)
- Target markdown: `{{document_path}}`

Output:

- Verification report: `{{tmp_dir}}/resolve-summary.md`
- events.jsonl: `{{events_path}}` (`{basename}.events.jsonl` in the same directory as `{{document_path}}`)
- Updated `{{document_path}}`

Procedure:

1. Read `{{tmp_dir}}/verifications/*.json` and Write the verification report to `{{tmp_dir}}/resolve-summary.md`. Format:
   - Heading: "# Review Verification Report"
   - Meta info: "Review document: {{document_path}}", "Verification date: YYYY-MM-DD" (bold)
   - Three tables under "## Verification Results":
     - "### Resolved": # / Severity / Trailing Field / Decision (outcome == Resolved)
     - "### Feedback Required": # / Severity / Trailing Field / Issue (outcome == Feedback)
     - "### Unresolved": # / Severity / Trailing Field / Memo (outcome == Unresolved)
   - "## Summary": bullet list with the number of findings verified / Resolved / Feedback Required / Unresolved
   - "## Feedback Details": for each finding with outcome == Feedback, include "### {finding-id} — Feedback", "Original finding (feedback_detail.description)", "Trailing field", "Actual state (feedback_detail.current_state)", "Issue (feedback_detail.issue)", "Suggestion (feedback_detail.suggestion)", separating entries with ---

2. Write verification events to `{{events_path}}` as JSONL, one event per line. Format: `{"id":"...","field":"verification","value":"<memo_value>"}`. Do not write entries with outcome == Unresolved.

3. Run `python .claude/scripts/render-review.py {{document_path}} {{events_path}} {{document_path}}`.
4. Remove with `.claude/scripts/rm-tmp.sh {{events_path}}`.

Return value: `{events_path, summary_path, summary_line (<=200 chars; e.g., "3 resolved, 1 feedback (M-1), 2 unresolved"), resolved_count, feedback_count, unresolved_count, template_id}`. Include the `template_id` value Read from this template's frontmatter as-is.
