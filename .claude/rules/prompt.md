# Prompt Discipline

When writing or modifying AI-facing prompts (agent definitions, skills, commands, rules, etc.), follow these rules.

These rules do not apply to human-facing documentation (README, API references, design docs, etc.). Human-facing documentation is governed by [document.md](document.md).

## The reader is an AI

- The reader of a prompt is an AI agent. Explanatory writing aimed at users or other humans is unnecessary.
- Do not write meta-commentary like "This prompt was created to ..." or "The following describes ...". Write the instruction itself.
- Limit explanations of background and motivation to what the AI needs for judgment. Do not include history that does not affect the AI's work.

### Meta-explanation patterns to avoid

The patterns below have no effect on AI runtime behavior, so remove them. The list items are example clusters; the final decision follows the Decision criterion at the end of this subsection. When multiple patterns coexist in a single sentence, remove only the matching portion and keep the actual instruction for the current step.

- Restating the effect of an action: "Do X. This causes Y to happen." / "By doing X, Y is achieved." / "Do X to propagate it to Z." (the effect of the immediately preceding action is inherent in the action itself)
- Emphasizing the intent of an action: "Be sure to Y." / "Do X without omission." / "Always do Z." (the imperative verb already conveys intent)
- Explanatory preambles: "In what follows ..." / "This section ..." / "Next, we describe ..." / "To do X, ..." (in-prompt or in-section structural narration that is self-evident from the body)
- Maintenance guidelines aimed at editors: "Avoid duplicate descriptions." / "Future maintainers should X." / "When editing the code, ..." (these address the editing of the SKILL or Rule itself, not actions the AI executes)
- Forward references to other steps: "This is not done in this step; it is done in Step N." / "Aggregation is performed in the next step." (information about other steps that the reader does not need to execute the current step; distinct from explanatory preambles, which describe in-prompt structure rather than other steps)

Decision criterion: if removing a sentence does not change the action the AI should take, remove it. This criterion applies to all items in the list above.

### Judgment WHY vs effect-restatement WHY

WHY explanations come in two kinds. Keep the first; remove the second.

- **Judgment WHY** (keep): constraints, hidden premises, or anti-misunderstanding notes the AI needs when judging edge cases.
  - Example: "cat heredoc breaks quoting on apostrophes inside values, so it cannot be used" (prevents the misunderstanding "is it OK if there are no apostrophes?").
  - Example: "Do not throw C++ exceptions across the C API boundary (prevents ABI mismatch)" (provides the basis of the constraint).
- **Effect-restatement WHY** (remove): restates the effect of the immediately preceding action.
  - NG: "Append it. This propagates it to the Sub." ← appending itself is the propagation.
  - NG: "Read it. This obtains the data." ← reading itself is obtaining.
  - Decision: is the effect inherent in the action itself? If yes, remove.

### Separation of maintenance guidelines from runtime instructions

- AI runtime instructions: concrete actions for the AI to read and execute. Write these in the prompt.
- Maintenance guidelines: editing policies for the prompt / skill / rule itself. They do not affect AI runtime.
  - NG: "Do not duplicate common rules." / "Do not include meta-explanations in Sub prompts." (these are policies for editing the prompt)
  - Treatment: consolidate them into a meta rule file like this one (prompt.md), or remove them. Do not write them in the body of an individual prompt.

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
