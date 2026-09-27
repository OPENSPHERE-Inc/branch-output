# Documentation Discipline

When writing or modifying documentation, follow these rules.

These rules do not apply to AI-facing prompts (agent definitions, skills, commands, rules, etc. under `.claude/`).

## Audience awareness

- **Documentation is written for third-party readers who consult it later, not for the user you're chatting with.** Avoid subjective writing that depends on the chat context. A third-party reader doesn't know "what the user asked for", "what the prior version of the doc said", or "which file something was ported from".
  - NG examples: Phrasings that presume the chat context, such as "Added per user request" or "Previously was X, now changed to Y".
  - Instead: Document only the facts, specifications, and assumptions readable from the doc itself, in a form independent of any conversation history. Leave change history in git log / PR descriptions; do not put it in the doc body.
- **Exception**: This does not apply when the user explicitly asks for such phrasing, or when the doc is itself an extension of the chat context (plans, reports, meeting notes, etc.).

## Reference independence

- **Do not reference matters that have not been documented in a form a third party can access.** Verbal decisions, Slack messages, chat history, unpublished material, etc. must not be cited as sources.
  - When citing, point to a place a third party can follow (a file in the repo, a public URL, a published spec, etc.).
  - When no such public reference exists, do not include the citation at all — rewrite the passage so the doc stands on its own.

## Markdown notation

- **Escape `|` inside inline code in table cells.** Markdown's table cell separator is `|` even inside inline code, so leaving it unescaped breaks parsing. Escape it as `\|`.
  - NG: `| operator | `a | b` represents OR |`
  - OK: `| operator | `a \| b` represents OR |`
- **Wrap Markdown source at a reasonable width (target: 100 characters max).** Don't pack a long paragraph onto one line; break at sentence or clause boundaries to preserve readability and diff clarity.
  - Exception: Places where Markdown forbids line breaks (table rows, heading lines, etc.).
- **A single newline in Markdown source does not produce a line break in the rendered output.** The viewer treats source newlines as whitespace and joins them into one paragraph. To split lines visually, use one of:
  - Separate paragraphs: insert a blank line.
  - Separate items: use a list (`-` or `1.`).
  - In-paragraph line break (use sparingly): two trailing spaces at end of line, or a `<br>` tag.
