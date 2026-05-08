---
name: analyze
description: Prompt for the analysis sub-agent in review-resolve Step 1 that extracts each finding's id / verification assignee from the review document
template_id: 5d9e2c8a-1f74-4b63-a9d8-3c5f7e1b9a42
---

Read the review document `{{document_path}}` and extract each finding's id / verification assignee (no file output). Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Targets to extract: Critical / Major / Minor (skip Info). Include all findings in `by_assignee` regardless of METADATA marker state (the verify Sub handles Resolved / Feedback / Unresolved decisions, so untriaged or estimate-incomplete findings are also dispatch targets).

Determining the verification assignee:

- If the Triage line contains "(assignee: {specialist})", use that specialist.
- If no assignee is present (markers empty / Triage is 🚫 Won't Fix with no assignee field, etc.), select one required specialist (cpp-sensei / qt-sensei / obs-sensei / network-sensei) from the finding's Reviewers, prioritizing those. If no required specialist is present, use the first entry in Reviewers. If Reviewers is empty, use cpp-sensei as the final fallback.

Return value: `{total, by_assignee: [{assignee, ids: [id, ...]}], template_id}`. Include the `template_id` value Read from this template's frontmatter as-is.
