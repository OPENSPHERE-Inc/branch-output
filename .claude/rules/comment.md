# Comment Discipline

When writing or modifying code, follow these rules for comments.

- **Fix the code, don't write an essay justifying it.** Multi-paragraph comments defending "why this is safe enough" hurt readability and invite re-litigation in later reviews.
- **Defer with a FIXME, not a rationale.** When the root-cause fix is out of scope (needs its own PR, design decision pending, intentional deferral), leave a short `FIXME:` / `TODO:` — one or two lines stating the problem and the recommended fix direction — instead of an exhaustive explanation.
- **Longer comments are for genuinely non-obvious invariants.** Only when the invariant cannot be inferred from the code itself. Even then, keep it to one tight paragraph.
- **Comments are written for third-party readers who will read the code later, not for the user you're chatting with.** Avoid subjective writing that depends on the chat context. A third-party reader doesn't know "what the user asked for", "what the prior code looked like", or "which file the logic was ported from".
  - NG example: After refactoring logic from `a` into `b`, writing `// Original logic from a, modified in b as ...`. The third-party reader has no knowledge of `a` or of the porting history — chat-context-dependent wording doesn't work for them.
  - Instead: If a comment is needed, document only the invariants, constraints, or assumptions that cannot be read from the code itself, in a form that stands alone within the codebase. Leave change history in git log / PR descriptions; do not put it in comments.

These rules apply to code comments, not to user-facing documentation (API references, README, etc.), which have their own audience and constraints.
