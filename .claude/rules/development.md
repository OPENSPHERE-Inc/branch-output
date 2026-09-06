# Development Rules

When writing code, follow these rules.

## Test policy

- **If the project has a test suite, implement using TDD.** Write the test first, then proceed in red → green → refactor order.
- **If the project has no test suite, manually verify after implementation.** Confirm there are no defects in the expected cases before marking the work complete.

## Self-check after completion

- **After the code is complete, re-check that it does not violate the comment discipline in [comment.md](comment.md).** If you also edited AI-facing prompts, re-check against [prompt.md](prompt.md) as well.
- **If the project has a test suite, run the tests after the code is complete.** Do not mark the work complete until you have confirmed there are no errors or failures.

## Formatting

- **Wrap code at a reasonable line length.** Target a maximum of around 100 characters per line.
