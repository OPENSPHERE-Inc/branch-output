---
name: triage
description: Prompt for the triage sub-agent that performs stage classification and triage decisions for each finding in review-respond Step 1
template_id: 1e9c4f7a-5b82-4d63-a1c8-3f7d2e9b4a15
---

As the initial-triage owner of the review document, Read `{{document_path}}`, perform stage classification and the triage decision for each finding, and Write the result to `{{tmp_dir}}/triage.json`. Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Extraction targets: Critical / Major / Minor sections (skip Info). For each finding, obtain id (C-1, M-1, mi-1, etc.) / severity / location / description (the body up to the marker) / current_meta (the current values of triage / estimate / status / verification; when the same field appears multiple times, use the last value).

Stage classification (based on current_meta):

- Marker is empty → pending_triage
- triage: 🔧 Will Fix, no estimate → pending_estimate
- estimate: ▶️ Maintain or 🚧 Alternative, no status → pending_fix
- verification last value is 💬 Feedback → feedback (re-fix target)
- triage: 🚫 Won't Fix → wontfix_skip
- estimate: 🔻 Downgrade → downgrade_skip
- status: 🟢 Fixed, no verification or last value is ✅ Verified → fixed_skip

Only findings whose stage is pending_triage or feedback are decision targets. For other stages, count only and do not perform a triage decision.

Decision categories:

- Will Fix — Valid; should be addressed.
- Won't Fix — Not applicable / false positive / risk accepted (reason required).
- Needs Investigation — Settle on Will Fix / Won't Fix after investigating the source.

Won't Fix guideline (when any of the following applies):

1. Out of scope of the branch diff.
2. Existing-code bug (not introduced by the branch).
3. Hypothesis error / technical mistake.
4. Inferable as acceptable from the project's purpose, use case, or assumed users.
5. Preference-based refactoring (no rationale grounded in correctness, safety, performance, or maintainability).
6. Reproducibility unclear; e2e verification needed.

High-severity exception: For Critical / Major Won't Fix, explicitly state "recommend separate PR" in the reason field (e.g. "Won't Fix — Existing-code bug. Recommend fixing in a separate PR.").

Specialist assignment (Will Fix only): Choose the most suitable from cpp-sensei / qt-sensei / obs-sensei / network-sensei / av-sensei / devops-sensei / python-sensei / lua-sensei.

`{{tmp_dir}}/triage.json` format: `{items: [{id, verdict, assignee (null for Won't Fix), reason, memo_value}], will_fix_count, wontfix_count, by_stage: {<stage>: <int>}}`

memo_value format:

- Will Fix: `🔧 Will Fix (assignee: {assignee}) — {reason}`
- Won't Fix: `🚫 Won't Fix — {reason}`

Return value: `{path, will_fix_count, wontfix_count, by_stage, by_assignee: [{assignee, ids: [id, ...]}], template_id}` (by_assignee groups Will Fix only by assignee. Do not include reason / memo_value or other body content in the return value). Include `template_id` (Read from this template's frontmatter) verbatim in the return value.
