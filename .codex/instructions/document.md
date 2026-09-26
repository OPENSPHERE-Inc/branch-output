# Documentation Discipline

Apply these rules when writing or modifying human-facing documentation. AI-facing prompts under `.codex/` follow
[prompt.md](prompt.md) instead.

## Audience awareness

- Write for third-party readers who may consult the document later. Do not refer to the current chat, the user's request,
  a previous version, or porting history.
- Document facts, specifications, and assumptions that stand on their own. Keep change history in the Git log or pull
  request description.
- Chat-derived wording is acceptable only when the user explicitly requests it or when the document is itself a plan,
  report, meeting note, or other extension of the conversation.

## Reference independence

- Cite only sources a third party can access, such as repository files and public specifications.
- Rewrite unsupported statements so they stand on their own instead of citing private conversation or unpublished
  material.

## Markdown notation

- Escape `|` as `\|` inside inline code in table cells.
- Target roughly 100 characters per source line, except where Markdown syntax requires a single line.
- Use blank lines, lists, two trailing spaces, or `<br>` when a rendered line break is required. A single source newline
  is normally rendered as whitespace.
