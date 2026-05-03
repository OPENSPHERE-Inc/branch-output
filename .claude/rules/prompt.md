# Prompt Discipline

When writing or modifying AI-facing prompts (agent definitions, skills, commands, rules, etc.), follow these rules.

These rules do not apply to human-facing documentation (README, API references, design docs, etc.). Human-facing documentation is governed by [document.md](document.md).

## The reader is an AI

- The reader of a prompt is an AI agent. Explanatory writing aimed at users or other humans is unnecessary.
- Do not write meta-commentary like "This prompt was created to ..." or "The following describes ...". Write the instruction itself.
- Limit explanations of background and motivation to what the AI needs for judgment. Do not include history that does not affect the AI's work.

## Structure and decoration

- Excessive decoration is unnecessary. Keep heading hierarchy minimal.
- Use the minimum line breaks required. Separating paragraphs with blank lines is enough.
- As a rule, do not use tables. Replace them with bullet lists.
  - Exception: When specifying an output format for the AI, showing a table as an output example is acceptable.
- Use emphasis (`**` and `*`) only for genuinely important points. Overuse dilutes the meaning.

## Eliminating context dependence

- Do not write in a way that depends on the chat context (same policy as [document.md](document.md)).
- Avoid phrasings that presume conversation history, such as "Because the user said ..." or "In the previous prompt ...". Each prompt must stand alone.

## What to include

- Include only the instructions and context the agent needs to do its work.
- Drop digressions, supplementary remarks, and "just-in-case" information. AI accuracy degrades with verbose instructions.
- Write instructions concretely. Avoid vague directives ("appropriately", "nicely", etc.); state the judgment criteria explicitly.

## Output format instructions

- When the AI outputs Markdown, note that Markdown viewers do not render single newlines as line breaks.
  - To split lines visually, use a list (`-` or `1.`) or separate paragraphs with a blank line.
  - Just inserting `\n` results in a single paragraph with whitespace separating the parts after rendering.
- When instructing the AI to produce Markdown output, write the format spec with this in mind.
