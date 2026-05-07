# Sub-Agent Common Rules

Common prohibitions that sub-agents of the review skills (parallel-review / review-respond / review-resolve / review-rounds) must observe.

## Prohibitions

- **No nesting**: Sub-agents must not launch further sub-agents via the Agent tool (the leader is the sole orchestrator).
- **Output scope**: Writing to any file other than the output file specified by the SKILL is prohibited. The following are allowed only for the responsible sub-agent (prohibited for any sub-agent that is not the responsible one):
  - Review document markdown update (via render-review.py): aggregator sub-agent.
  - events.jsonl write: aggregator sub-agent.
  - Build commands (cmake / make / build.ps1 / pwsh build scripts, etc.): format & build verification sub-agent.
  - Formatters (clang-format / cmake-format): format & build verification sub-agent.
- **Source editing**: Permitted only for the fix sub-agent and the build-fix specialist sub-agent (other sub-agents may only Read sources).

## Tools

Use the Write tool for file output. Bash cat heredoc is unusable because apostrophes inside values (e.g., `Won't`) break the outer quoting.

## Coding Conventions

When editing source code, follow `.claude/rules/comment.md`. When editing human-facing documentation, follow `.claude/rules/document.md`.

## Launch prompt completeness

Each SKILL leader MUST include the following 4 items in the launch prompt when launching a sub-agent:

- The path to the external template (with a `Read` directive, stating that this is the first action to perform)
- Variable values that substitute the `{{...}}` placeholders in the template
- Round-specific overrides (state "(none)" explicitly when there are none)
- The requirement to include `template_id` (the value Read from the template frontmatter) in the return value

Omitting or modifying variable values, and quoting / summarizing / restating the template body, are prohibited. If the `template_id` in the return value received by the leader does not match the template's frontmatter, relaunch a fresh instance of the same `subagent_type` and retry. If two consecutive runs of the same sub-agent fail to match, report the failure to the user and abort.
