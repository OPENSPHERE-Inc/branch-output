---
name: scope-analysis
description: Prompt for the scope-analysis sub-agent in parallel-review Step 1, which analyzes the diff and selects required reviewer candidates
template_id: b3e2f1a7-9c84-4d56-8e3b-7f1a4c9d2e85
---

As the review-scope analysis owner, Read `{{tmp_dir}}/diff.txt` to count lines and select required reviewer candidates. Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

User-explicitly-requested reviewers: `{{user_requested}}` (may be an empty array)

Reviewer candidate mapping (kind / selection condition):

- cpp-sensei (required): always add
- qt-sensei (required): always add
- obs-sensei (required): always add
- network-sensei (required): always add
- av-sensei (optional): the diff contains changes to encoder settings / media pipeline / A/V quality / streaming output settings
- devops-sensei (optional): the diff contains changes to CMakeLists.txt / *.cmake / .github/workflows / build.ps1 / .clang-format / .cmake-format / Inno Setup / packaging settings
- python-sensei (optional): the diff contains changes to .py files
- lua-sensei (optional): the diff contains changes to .lua files

For each reviewer's specialty area and perspective, see the agent definition (`.claude/agents/{name}.md`). This mapping only provides selection criteria.

Procedure:

1. Add all required reviewers to `recommended` (reason: `"required"`).
2. Add optional reviewers only when the selection condition matches (record the matching extension/path in `reason`).
3. Add any `user_requested` reviewers not already added (reason: `"user explicitly requested"`).
4. `line_count` = total of +/- lines in the diff.

Return value: `{line_count, recommended_reviewers: [{name, reason}], extension_summary (e.g., ".cpp(12), .hpp(5)"), rationale (1-2 sentences justifying optional additions), template_id}`. Include `template_id` exactly as Read from this template's frontmatter.
