# Code Review Rules

When conducting code reviews, follow these rules.

- **The review goal is code quality, not nuance-perfect prose in comments.** Flag real bugs, risks, and unclear code. Do not demand that comments be rewritten into multi-paragraph essays just to cover every technical subtlety.
- **Raise a comment-related finding only when the comment actively misleads.** A comment is a legitimate finding only if it could cause a maintainer to introduce a bug. Minor wording polish is Info at most, and often not worth raising at all.
- **Prefer FIXME over prose.** When a real problem is deferred to a future PR (root-cause fix out of scope, design decision pending, etc.), recommend a short `FIXME:` / `TODO:` comment pointing at the issue — not a multi-paragraph justification of why the current code is "acceptable enough."
- **Downgrade refactoring suggestions that aren't coding convention violations, bugs, or syntax errors.** Plain structural improvements, naming cleanups, and similar suggestions are Info at most, and often not worth raising at all.
