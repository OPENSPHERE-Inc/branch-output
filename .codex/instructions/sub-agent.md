# CReview Sub-Agent Rules

Apply these rules when working as a sub-agent for `creview:start`, `creview:respond`, `creview:resolve`, or
`creview:rounds`.

## Boundaries

- Do not spawn another sub-agent. The parent agent owns orchestration.
- Write only to the output paths assigned by the parent or active template.
- Edit source only when assigned a fix or build-fix role.
- Run build commands or formatters only when assigned format or build verification.
- Follow [comment.md](comment.md) for code comments and [document.md](document.md) for human-facing documentation.

## Template contract

When the parent supplies an external template:

- Read the template before other task work.
- Use the provided placeholder values and round-specific overrides without rewriting them.
- Include the template's frontmatter `template_id` in the return value.
- If the expected output is structured, preserve its field names, types, and nesting exactly.
