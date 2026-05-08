---
name: verify
description: Prompt for the verification sub-agent in review-resolve Step 2 that verifies its assigned findings in batch
template_id: 8a1f5c9b-2e73-4d64-9c1e-8b3d7f2a5e94
---

Verify the assigned findings `{{ids}}` in batch. Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Input: review document `{{document_path}}` (Read it to obtain each id's severity / location / description / trailing field).

For each id, determine Resolved / Feedback / Unresolved using the logic below and Write the result to `{{tmp_dir}}/verifications/{id}.json`. The trailing field is the final value among triage / estimate / status / verification within the METADATA markers.

Common additional checks (always perform immediately before the code-verification branch decision; apply in each of the `Status: 🟢 Fixed` / `Triage: 🚫 Won't Fix` / `Estimate: 🔻 Downgrade` branches):

- If comments were added or modified, Read `.claude/rules/comment.md` and check for violations. If any violation exists, mark as Feedback.
- If human-facing documentation (README, API references, etc.; AI-facing prompts under `.claude/` are out of scope) was added or modified, Read `.claude/rules/document.md` and check for violations. If any violation exists, mark as Feedback.

`Status: 🟢 Fixed` present:

1. Read the referenced file and lines to confirm the described fix actually exists:
   - Estimate ▶️ Maintain: the normal fix for the finding (including logic changes) is fully reflected.
   - Estimate 🚧 Alternative: a `FIXME:` / `TODO:` comment is present at the relevant location, roughly aligned with the FIXME direction in the Estimate, and sufficient for the future fix (logic changes are not expected).
2. Check for newly introduced issues (regressions / bugs / style violations / thread safety / resource leaks, etc.).
3. Perform the common additional checks.
4. Decision: Resolved (accurate, complete, no new issues) / Feedback (missing, incomplete, or new issues; describe the remaining work).

`Triage: 🚫 Won't Fix` present:

1. Read the referenced file and evaluate whether the rationale for "not fixing" is still valid against the current code.
2. Perform the common additional checks.
3. Decision: Resolved (rationale is valid) / Feedback (rationale is flawed or the situation has changed; describe the reason).

`Estimate: 🔻 Downgrade` present:

1. Read the referenced file and evaluate whether the downgrade rationale (diffusion signals / Cost / Future / reason) is valid against the current code.
2. Confirm whether a separate-PR recommendation is present and appropriate (be especially careful for Critical / Major findings without a separate-PR recommendation).
3. Perform the common additional checks.
4. Decision: Resolved (rationale is valid) / Feedback (rationale is flawed or the situation has changed; describe the reason).

Cases reported as Unresolved:

- `Estimate: 🚧 Alternative` present, no `Status` — FIXME not yet added.
- `Estimate: ▶️ Maintain` present, no `Status` — fix not yet completed.
- Only `Triage: 🔧 Will Fix` — estimate not yet completed.
- No metadata between the markers — not yet triaged.

Format of `{{tmp_dir}}/verifications/{id}.json`: `{id, severity, trailing_field, outcome (Resolved | Feedback | Unresolved), reason (1–3 sentences), memo_value, feedback_detail}`

trailing_field: the trailing field within the markers (e.g., `Status: 🟢 Fixed` / `Triage: 🚫 Won't Fix` / `(empty)`).

memo_value:

- Resolved: `✅ Verified — {verification result}`
- Feedback: `💬 Feedback — {what is missing and what is required for full resolution}`
- Unresolved: `""`

feedback_detail (include only when outcome == Feedback): `{description, current_state, issue, suggestion}`

Return value: `{items: [{id, outcome}, ...], template_id}` (do not include reason / memo_value / feedback_detail or other body content in the return value; write them only to `verifications/{id}.json`). Include the `template_id` value Read from this template's frontmatter as-is.
