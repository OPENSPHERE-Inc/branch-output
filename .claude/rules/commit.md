# Commit Rules

Rules that apply to every commit made in this repository.

## Language

- Write the commit message in the language used by the repository's existing commit history (check `git log`).

## Format

- Keep the commit message concise. A single-line summary is preferred.
- When the change is complex enough that one line cannot cover it, additional body paragraphs after a blank line are acceptable. Keep them short.

## Content to avoid

- Do NOT include workflow-specific labels such as "Review response", "レビュー対応", or similar. The commit message should describe **what the code does now**, not the process that produced it.
- Do NOT include finding IDs from review documents (e.g. `C-1`, `M-1`, `m-1`, `I-1`). These are internal to the review workflow and have no meaning outside of it.

## Trailers

- Do NOT add trailers such as `Co-Authored-By:`, `Signed-off-by:`, or any AI-attribution line unless the user explicitly requests it.
