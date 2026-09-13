# Prompt Discipline

Apply these rules when writing or modifying AI-facing prompts, custom-agent definitions, skills, task templates, or
instruction files. Human-facing documentation follows [document.md](document.md).

## Contract-style prompts

- State the goal concretely and leave implementation choices to the executing agent unless reproducibility requires a
  fixed procedure.
- State contracts explicitly: required commands, checkpoints, output fields, and formats.
- Put strict or lengthy procedures in scripts when practical; instruct the agent to run the script and verify its result.

## Write for an AI reader

- Omit human-facing introductions and meta-commentary about why the prompt exists.
- Include only context that changes the agent's decisions.
- Remove a sentence when removing it does not change the action the agent should take.
- Keep explanations of hidden constraints and edge cases. Remove explanations that merely restate the effect of the
  preceding command.
- Keep prompt-maintenance policy out of runtime prompts.

## Structure and context

- Use the minimum heading hierarchy, decoration, and line breaks needed for clarity.
- Prefer prose or lists over tables unless a table defines an output contract.
- Make each prompt understandable without chat history.
- Use concrete decision criteria instead of vague requests such as "appropriately" or "nicely".

## Markdown output contracts

- Use lists or blank lines when separate rendered lines are required. A single source newline is normally rendered as
  whitespace.
