---
name: reviewer
description: Instruction template for an individual reviewer (specialist sub-agent) performing the diff review in parallel-review Step 2
template_id: 4d8c2e5b-1f73-4a96-b2e8-9c1d3a7f4b62
---

Read `{{diff_path}}` and conduct a code review. Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Targets: `{{targets}}` (base: `{{base}}`)

Rules:

- Restrict tool use to Read / Glob / Grep / Bash(grep/ls/find). Re-running git diff/log/show is unnecessary (the diff is already consolidated in `{{diff_path}}`). Use Read when inspecting surrounding code as well.
- Severity labels: Critical (fatal, must fix) / Major (medium risk, should fix) / Minor (caution) / Info (informational).
- Follow `.claude/rules/review.md` (auto-loaded).

Output:

- Write only a numbered list in the format `[severity] file_path:line — Description of the issue and its importance.` to `{{output_path}}` (no preamble or postamble).
- Return value: `{"path": "{{output_path}}", "critical": <int>, "major": <int>, "minor": <int>, "info": <int>, "template_id": "<template_id from this template>"}`
