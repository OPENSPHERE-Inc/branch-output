# Development Rules

Apply these rules when writing code.

## Test policy

- If the project has a test suite, use test-driven development: red, green, then refactor.
- If the project has no test suite, manually verify the expected behavior before completing the work.

## Completion checks

- Re-check modified code against [comment.md](comment.md).
- Re-check AI-facing prompt changes against [prompt.md](prompt.md).
- Run the project's tests when a test suite exists, and do not report completion while errors or failures remain.

## Formatting

- Follow the repository formatter and style configuration. Keep lines reasonably short when the formatter does not
  decide the layout.
