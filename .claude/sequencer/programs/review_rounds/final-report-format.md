<!--
Template referenced by the review-rounds sequencer program (../review_rounds.py)
during its final-report-generation Instruction. The agent replaces the `<...>`
placeholders and the "..." cells in tables with real data to produce the report.
-->

# Code Review Final Report

**Branch:** `<branch-name>`
**Date:** `YYYY-MM-DD`
**Rounds executed:** `<N>`
**Termination reason:** `<Converged: zero findings / Converged: no source code changes / Round loop stopped by user request / Maximum round count N reached>`

## Statistics Summary

| Round | Findings | 🔧 Will Fix | ▶️ Maintain | 🚧 Alternative | 🔻 Downgrade | 🟢 Fixed | Unresolved | Feedback re-fixes |
|---------|-------:|------------:|------------:|---------------:|-------------:|---------:|-----------:|------------------:|
| Round 1 |    ... |         ... |         ... |            ... |          ... |      ... |        ... |               ... |
| Round 2 |    ... |         ... |         ... |            ... |          ... |      ... |        ... |               ... |
| **Total** | **...** |    **...** |    **...** |       **...** |     **...** |  **...** |   **...** |          **...** |

Column definitions:

- **Unresolved**: number of findings whose `Verification: 💬 Feedback` still remains
  after 3 attempts of the feedback re-fix loop.
- **Feedback re-fixes**: number of feedback re-fix loop iterations executed in this
  round (max 3).

## All Findings and Resolution Status

### Resolved

Aggregate findings with `Status: 🟢 Fixed` (covers both regular Maintain fixes and
Alternative FIXME insertions).

| # | Round | Severity | Location | Finding Summary | Resolution |
|---|---------|----------|-------------|-----------------|------------|
| 1 | Round 1 | Critical | `file:line` | Summary | 🟢 Fixed (Maintain) — fix description |
| 2 | Round 1 | Major    | `file:line` | Summary | 🟢 Fixed (Alternative) — FIXME comment added at `<file:line>` |

### Unresolved

Aggregate findings whose `Verification: 💬 Feedback` remained.

| # | Round | Severity | Location | Finding Summary | Status |
|---|---------|----------|-------------|-----------------|--------|
| 1 | Round 2 | Major | `file:line` | Summary | Not resolved even after feedback re-fix |

### Decided as No Action Needed

List here all `Triage: 🚫 Won't Fix` and `Estimate: 🔻 Downgrade` findings that
**do not carry a separate-PR recommendation** (false positives, preference issues,
fully-rejected out-of-scope items, Downgrades with neither alternative response
nor separate-PR recommendation, etc.).

| # | Round | Severity | Location | Finding Summary | Reason |
|---|---------|----------|-------------|-----------------|--------|
| 1 | Round 1 | Minor | `file:line` | Summary | Triage: 🚫 Won't Fix — reason |
| 2 | Round 1 | Minor | `file:line` | Summary | Estimate: 🔻 Downgrade — Cost L, Signals a,b. Neither alternative response nor separate-PR recommendation. |

## Recommended for Future Action

Aggregate findings whose follow-up in a separate PR is anticipated:

- `Triage: 🚫 Won't Fix` findings whose rationale explicitly recommends a separate PR.
- `Estimate: 🔻 Downgrade` findings whose rationale explicitly recommends a separate PR.
- `Estimate: 🚧 Alternative` (FIXME already inserted) findings whose rationale
  explicitly recommends a separate PR.

This is mutually exclusive with "Decided as No Action Needed". Alternative FIXME
insertions are also listed in the "Resolved" section, but if they carry a
separate-PR recommendation, list them additionally here (for roadmap purposes).

| # | Severity | Location | Summary | Reason for Recommendation |
|---|----------|-------------|---------|---------------------------|
| 1 | Minor | `file:line` | Summary | Triage: 🚫 Won't Fix — existing-code bug. Recommend addressing in a separate PR. |
| 2 | Major | `file:line` | Summary | Estimate: 🔻 Downgrade — Cost L, Signals a,b,c. Recommend addressing in a separate PR. |
| 3 | Major | `file:line` | Summary | Estimate: 🚧 Alternative — FIXME already added (`output.cpp:200`). Recommend full fix in a separate PR. |

## Review Document Index

| Round | Review Document |
|---------|-----------------|
| Round 1 | `<path>` |
| Round 2 | `<path>` |
