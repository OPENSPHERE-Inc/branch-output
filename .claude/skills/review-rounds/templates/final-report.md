<!--
Final report template for review-rounds. The agent replaces `<...>` placeholders and
table "..." cells with actual data to generate the report.
This file is kept in sync with .claude/sequencer/programs/review_rounds/final-report-format.md.
-->

# Code Review Final Report

**Branch:** `<branch-name>`
**Date:** `YYYY-MM-DD`
**Rounds executed:** `<N>`
**End reason:** `<converged with zero findings / converged with no source code changes / round loop stopped by user / reached max rounds N>`

## Statistics summary

| Round | Findings | 🔧 Will Fix | ▶️ Maintain | 🚧 Alternative | 🔻 Downgrade | 🟢 Fixed | Unresolved | Feedback re-fix attempts |
|-------|---------:|------------:|------------:|---------------:|-------------:|---------:|-----------:|-------------------------:|
| Round 1 |   ... |         ... |         ... |            ... |          ... |      ... |        ... |                      ... |
| Round 2 |   ... |         ... |         ... |            ... |          ... |      ... |        ... |                      ... |
| **Total** | **...** | **...** |    **...** |       **...** |     **...** |  **...** |    **...** |                **...** |

Column definitions:

- **Unresolved**: number of findings that still have `Verification: 💬 Feedback` even after running the feedback re-fix loop 3 times.
- **Feedback re-fix attempts**: number of attempts of the feedback re-fix loop executed in this round (max 3).

## Full findings list and response status

### Resolved

Aggregate findings with `Status: 🟢 Fixed` (includes both normal fixes via Maintain and FIXME attachments via Alternative).

| # | Round | Severity | Location | Finding summary | Response |
|---|-------|----------|----------|-----------------|----------|
| 1 | Round 1 | Critical | `file:line` | Summary | 🟢 Fixed (Maintain) — fix content |
| 2 | Round 1 | Major | `file:line` | Summary | 🟢 Fixed (Alternative) — FIXME comment attached at `<file:line>` |

### Unresolved

Aggregate findings that remain at `Verification: 💬 Feedback`.

| # | Round | Severity | Location | Finding summary | Status |
|---|-------|----------|----------|-----------------|--------|
| 1 | Round 2 | Major | `file:line` | Summary | Not resolved even after feedback re-fix |

### Judged not to require action

List findings with `Triage: 🚫 Won't Fix` and `Estimate: 🔻 Downgrade` that **do not have a separate-PR recommendation attached** (false positives, matters of preference, fully rejected as out of scope, Downgrades that come with neither an alternative response nor a separate-PR recommendation, etc.).

| # | Round | Severity | Location | Finding summary | Reason |
|---|-------|----------|----------|-----------------|--------|
| 1 | Round 1 | Minor | `file:line` | Summary | Triage: 🚫 Won't Fix — reason |
| 2 | Round 1 | Minor | `file:line` | Summary | Estimate: 🔻 Downgrade — Cost L, Signals a,b. No alternative response or separate-PR recommendation. |

## Recommended future actions

List findings expected to be addressed later in a separate PR, in the same section format as the review document, with the full body included.

Aggregation targets (any of the following, with a separate-PR recommendation explicitly stated in the reason field):

- `Triage: 🚫 Won't Fix`
- `Estimate: 🔻 Downgrade`
- `Estimate: 🚧 Alternative` (FIXME already attached)

Exclusion rule: Among the candidates above, exclude from this section any finding whose same-location, same-content counterpart was resolved as `Status: 🟢 Fixed` in a later round (already fixed, so no need to keep on the roadmap). Identity is judged by matching `file:line` and the finding summary.

This is mutually exclusive with the "Judged not to require action" section. Alternative FIXME attachments are also listed in the "Resolved" section, but when accompanied by a separate-PR recommendation, they are additionally listed in this section (for roadmap purposes).

### R1-C-1 — `file.cpp:42`

- **Severity:** Critical
- **Source round:** Round 1
- **Source ID:** C-1
- **Source reviewer:** cpp-sensei, obs-sensei
- **Decision:** Triage: 🚫 Won't Fix — bug in existing code. Recommended to fix in a separate PR.

**Finding:**

{Quote the full body of the corresponding finding from the source review document (do not include METADATA markers)}

---

### R2-M-3 — `output.cpp:200`

- **Severity:** Major
- **Source round:** Round 2
- **Source ID:** M-3
- **Source reviewer:** cpp-sensei
- **Decision:** Estimate: 🔻 Downgrade — Cost: L, Future: M, Signals: a,b,c — This response is recommended to be performed in a separate PR

**Finding:**

{Quote the full body of the corresponding finding from the source review document}

---

### R1-M-2 — `output.cpp:300`

- **Severity:** Major
- **Source round:** Round 1
- **Source ID:** M-2
- **Source reviewer:** qt-sensei
- **Decision:** Estimate: 🚧 Alternative — Cost: M, Future: S, Signals: b,d — FIXME already attached (`output.cpp:200`). Full response recommended in a separate PR.

**Finding:**

{Quote the full body of the corresponding finding from the source review document}

---

## Review document list

| Round | Review document |
|-------|-----------------|
| Round 1 | `<path>` |
| Round 2 | `<path>` |
