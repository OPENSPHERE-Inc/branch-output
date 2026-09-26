# Code Review Rules

Apply these rules when reviewing code.

- Prioritize real bugs, risks, and materially unclear code over prose polish.
- Raise a comment finding only when the comment could mislead a maintainer into introducing a bug.
- When a real problem is intentionally deferred, recommend a short `FIXME:` or `TODO:` that states the problem and fix
  direction instead of a long rationale.
- Treat structural improvements and naming cleanups as informational unless they reveal a bug, syntax error, or project
  convention violation.
