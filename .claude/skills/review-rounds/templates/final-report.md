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

Aggregate the following (findings expected to be addressed later in a separate PR):

- `Triage: 🚫 Won't Fix` items where a separate-PR recommendation is explicitly stated in the reason field
- `Estimate: 🔻 Downgrade` items where a separate-PR recommendation is explicitly stated in the reason field
- `Estimate: 🚧 Alternative` (FIXME already attached) items where a separate-PR recommendation is explicitly stated in the reason field

This is mutually exclusive with the "Judged not to require action" section. Alternative FIXME attachments are also listed in the "Resolved" section, but when accompanied by a separate-PR recommendation, they are additionally listed in this section (for roadmap purposes).

| # | Severity | Location | Summary | Recommendation reason |
|---|----------|----------|---------|------------------------|
| 1 | Minor | `file:line` | Summary | Triage: 🚫 Won't Fix — bug in existing code. Recommended to fix in a separate PR. |
| 2 | Major | `file:line` | Summary | Estimate: 🔻 Downgrade — Cost L, Signals a,b,c. This response is recommended to be performed in a separate PR. |
| 3 | Major | `file:line` | Summary | Estimate: 🚧 Alternative — FIXME already attached (`output.cpp:200`). Full response recommended in a separate PR. |

## Review document list

| Round | Review document |
|-------|-----------------|
| Round 1 | `<path>` |
| Round 2 | `<path>` |
