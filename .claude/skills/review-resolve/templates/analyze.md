---
name: analyze
description: Prompt for the analysis sub-agent in review-resolve Step 1 that extracts each finding's id / stage / verification assignee from the review document
template_id: 5d9e2c8a-1f74-4b63-a9d8-3c5f7e1b9a42
---

Read the review document `{{document_path}}` and extract each finding's id / stage / verification assignee (no file output). Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Targets to extract: Critical / Major / Minor (skip Info).

Stage classification (trailing value of current_meta):

- Empty between markers → pending_triage
- triage: 🔧 Will Fix, no estimate → pending_estimate
- estimate: ▶️ Maintain or 🚧 Alternative, no status → pending_fix
- verification's final value is 💬 Feedback → feedback
- triage: 🚫 Won't Fix → wontfix_skip
- estimate: 🔻 Downgrade → downgrade_skip
- status: 🟢 Fixed, no verification or final value is ✅ Verified → fixed_skip

Determining the verification assignee:

- If the Triage line contains "(assignee: {specialist})", use that specialist.
- If no assignee is present, select one required specialist (cpp-sensei / qt-sensei / obs-sensei / network-sensei) from the finding's Reviewers, prioritizing those. If no required specialist is present, use the first entry in Reviewers.

Return value: `{total, by_stage: {<stage>: <int>}, by_assignee: [{assignee, ids: [id, ...]}], template_id}`. Include the `template_id` value Read from this template's frontmatter as-is.
