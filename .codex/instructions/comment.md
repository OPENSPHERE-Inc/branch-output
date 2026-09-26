# Comment Discipline

When writing or modifying code, follow these rules for comments.

- **Fix the code, don't write an essay justifying it.** Multi-paragraph comments defending "why this is safe enough"
  hurt readability and invite re-litigation in later reviews.
- **Defer with a FIXME, not a rationale.** When the root-cause fix is out of scope, leave a short `FIXME:` or `TODO:`
  stating the problem and recommended fix direction in one or two lines.
- **Longer comments are for genuinely non-obvious invariants.** Use one tight paragraph only when the invariant cannot
  be inferred from the code.
- **Write comments for future third-party readers.** Do not refer to the current chat, a user request, the previous
  implementation, or porting history. Put change history in the Git log or pull request description.

These rules apply to code comments, not user-facing documentation.
