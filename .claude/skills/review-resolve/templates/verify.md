---
name: verify
description: Prompt for the verification sub-agent in review-resolve Step 2 that verifies its assigned findings in batch
template_id: 8a1f5c9b-2e73-4d64-9c1e-8b3d7f2a5e94
---

Verify the assigned findings `{{ids}}` in batch. Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Input: review document `{{document_path}}` (Read it to obtain each id's severity / location / description / trailing field).

For each id:

1. Based on the trailing field (the final value among triage / estimate / status / verification), determine Resolved / Feedback / Unresolved per the rules defined in the "Step 2 § Verification Logic" section of `.claude/skills/review-resolve/SKILL.md`.
2. Write to `{{tmp_dir}}/verifications/{id}.json`.

Format of `{{tmp_dir}}/verifications/{id}.json`: `{id, severity, trailing_field, outcome (Resolved | Feedback | Unresolved), reason (1–3 sentences), memo_value, feedback_detail}`

trailing_field: the trailing field within the markers (e.g., `Status: 🟢 Fixed` / `Triage: 🚫 Won't Fix` / `(empty)`).

memo_value:

- Resolved: `✅ Verified — {verification result}`
- Feedback: `💬 Feedback — {what is missing and what is required for full resolution}`
- Unresolved: `""`

feedback_detail (include only when outcome == Feedback): `{description, current_state, issue, suggestion}`

Return value: `{items: [{id, outcome}, ...], template_id}`. Include the `template_id` value Read from this template's frontmatter as-is.
