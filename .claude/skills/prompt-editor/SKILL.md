---
name: prompt-editor
description: Create and edit AI-facing prompts (agent / skill / rule / command, etc.) and self-check / fix them against prompt.md rules
allowed-tools: Agent, Read, Write, Edit, Glob, Grep, Bash(ls:*), Bash(mkdir:*), Bash(find:*)
---

# Prompt Editor

Create or edit an AI-facing prompt (markdown). After producing the output, self-check it against `.claude/rules/prompt.md` and fix any violations.

Human-facing documentation (README, API references, design docs, etc.) is out of scope. That is governed by `.claude/rules/document.md`.

## Input

The user specifies one of:

- New: kind (agent / skill / rule / command / prompt) + target path + requirements
- Edit: path to an existing file + edit requirements

If the argument is `$ARGUMENTS`, interpret it as the above. If the kind is not explicitly given, infer it from the target path:

- `.claude/agents/{name}.md` → agent
- `.claude/skills/{name}/SKILL.md` → skill
- `.claude/rules/{name}.md` → rule
- `.claude/commands/{name}.md` → command
- Anything else (e.g. a sub-agent prompt body extracted from a skill) → prompt

## Step 1 — Produce the output

### New

1. Read the template for the kind:
   - agent → `.claude/skills/prompt-editor/templates/agent.md`
   - skill → `.claude/skills/prompt-editor/templates/skill.md`
   - rule → `.claude/skills/prompt-editor/templates/rule.md`
   - command → `.claude/skills/prompt-editor/templates/command.md`
   - prompt → `.claude/skills/prompt-editor/templates/prompt.md`
2. Fill in the placeholders (`{...}`) based on the requirements. Sections not addressed by the requirements may be removed. Add sections that are not in the template if the requirements call for them.
3. If the target path's directory does not exist, create it with `mkdir -p`.
4. Write the file at the target path.

### Edit

1. Read the target path.
2. Edit the file based on the requirements.

### When embedding a sub-agent prompt inside a code block

When a prompt is to be passed to the Agent tool and you embed it inside a triple-backtick code block in the body, write it as plain text:

- Do not use headings (`#` / `##`, etc.), tables, or emphasis (`**` / `*` / `__`).
- Bullet lists (`-` or `1.`) for genuine enumerations and code / JSON snippets are allowed.
- Use `{...}` for placeholders (variables filled by the caller).

### Externalize markdown output templates

When the prompt being edited contains a "markdown output template" (a skeleton of the produced markdown), do not embed it in a code block in the body. Split it out to an external file and reference it (avoids markdown-in-markdown):

- Location: `templates/{name}.md` under the relevant skill.
- In the body, leave only a path reference of the form: `Template: .claude/skills/{skill-name}/templates/{name}.md ({consumer} reads it to learn the skeleton).`
- Existing example: this skill itself externalizes per-kind templates under `.claude/skills/prompt-editor/templates/` and references them from Step 1.

Exceptions (embedding in a code block is allowed):

- Input format examples (descriptions of documents / data formats the prompt being edited consumes).
- Examples in non-markdown formats such as JSONL / JSON / command lines.

## Step 2 — Self-check

1. Read `.claude/rules/prompt.md`.
2. Read the file produced in Step 1.
3. List any violation against each item in prompt.md as `path:line`.
4. If Step 1 included a sub-agent prompt code block, also check the plain-text rule inside the code block.
5. Check whether the body embeds an output template that starts with a triple backtick and language tag `markdown` (input-format examples and non-markdown formats are excluded). If found, externalize it in Step 3 and replace it with a path reference.

## Step 3 — Fix violations

If Step 2 detects violations, fix them with Edit. After fixing, re-run Step 2 to make sure no new violations were introduced. Repeat up to 2 times. If violations remain after the second pass, present the remaining list to the user and ask for judgment.

## Step 4 — Compress

Reduce verbosity in the file produced in Step 1, including any sub-agent prompts embedded in code blocks within it. The format rules from Step 1 (e.g., plain text inside sub-agent prompt code blocks) still apply during compression. Apply Edit to remove the following:

- Polite forms such as "please ..." → use the imperative.
- The same rule repeated in multiple places → consolidate or replace with a reference.
- Statements the AI would naturally infer (e.g., per-edge-case instructions for self-evident cases) → remove.
- "A is the case, therefore not B" wording (the `x == a && x != b` pattern) → keep only the first half.
- Unnecessarily verbose code examples or templates → trim to the minimum skeleton.

Decision criterion: if removing the text does not change the action the AI should take, remove it. Keep judgment WHY (constraints, premises, anti-misunderstanding notes).

## Step 5 — Test

Have a sub-agent read the file produced in Step 1 and verify whether it is interpreted without missing or misleading information (in particular, whether Step 4's compression damaged any meaning).

1. Build a checklist. Each item is a question of the form "what should an agent reading this prompt do in situation X?" (examples: how to determine the kind when the input does not specify it, the fallback when step X fails, the input/output flow between steps, the threshold for each branch). Cover the input spec, each step, each branch condition, and the output spec.
2. Launch the test sub-agent via the Agent tool. Prompt:

```
Prompt-interpretation tester. Read the target prompt and answer each checklist item. Do not propose fixes or edit.

Target prompt: {prompt_path}

Checklist:
1. {item 1}
2. {item 2}
...

Steps:
1. Read {prompt_path}.
2. For each item, answer in 1-2 sentences using only what is readable from the prompt. Do not use external knowledge or guesswork.
3. If an item cannot be answered from the prompt, or is ambiguous, or is contradictory, report it as unclear with a one-sentence reason.

Return value: {pass, unclear_items: [{item, reason}]} (pass: true if unclear_items is an empty array)
```

3. If the return value is `pass: true`, finish.
4. If `unclear_items` are present, fix the relevant places with Edit and rerun this step. Repeat up to 3 times. If unclear items remain after the third pass, present the remaining list to the user and ask for judgment.

## Step 6 — Report

Report the target path, the number of violations detected/fixed, whether compression occurred, and the test result (pass or remaining unclear items).
