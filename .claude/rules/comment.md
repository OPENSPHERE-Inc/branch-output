# Comment Discipline

When writing or modifying code, follow these rules for comments.

- **Fix the code, don't write an essay justifying it.** Multi-paragraph comments defending "why this is safe enough" hurt readability and invite re-litigation in later reviews.
- **Defer with a FIXME, not a rationale.** When the root-cause fix is out of scope (needs its own PR, design decision pending, intentional deferral), leave a short `FIXME:` / `TODO:` — one or two lines stating the problem and the recommended fix direction — instead of an exhaustive explanation.
- **Longer comments are for genuinely non-obvious invariants.** Only when the invariant cannot be inferred from the code itself. Even then, keep it to one tight paragraph.

These rules apply to code comments, not to user-facing documentation (API references, README, etc.), which have their own audience and constraints.
