# Branch Output {plugin_version} regression test (macOS)

- Work folder: {path of `<work>`}
- Build: {commit or bundle path}, `Contents/MacOS/osi-branch-output` SHA-256 {hash}
- OBS: {version}: {asset name}, verified by {digest / size} (one line per version)
- Machine: {macOS version, chip, available hardware encoders, Xcode used for the build}
- Date: {YYYY-MM-DD}

## Summary

{Counts of PASS / FAIL (new, known issue) / SKIP / BLOCKED per version, and the conclusion in two or three sentences.}

## Results

| ID | Case | 32.2 | 31.1 | 30.1.2 | Notes |
|---|---|---|---|---|---|
| R01 | Startup and new filter | {result} | {result} | {result} | {short note} |
| M01 | Add and configure | {result} | {result} | {result} | {short note} |

## Failures

### {ID} on {version}: {one-line summary}

- Operation: {what was done}
- Expected: {Pass item}
- Actual: {what happened}
- Evidence: {files in this folder: log excerpts, ffprobe output, crash report}
- Reproduced on retry: {yes / no}
- Related issue: {#N, new, or new (regression of #N)}

## Observations

Behavior worth noting that did not fail a case, grouped as below. Leave out an empty group.

- Environment
  - {A problem of the machine or the operation, and how it was worked around}
- OBS
  - {Behavior of OBS itself, and the evidence that OBS shows it without the plugin}
- Plugin, known issues
  - {Behavior matching an open issue or an issue closed as not planned, with the case ID and the issue number}
- Plugin, no issue
  - {Behavior with no matching known issue, with the case ID and `regression of #N` when it matches an issue closed as completed: either not judged a defect, with the reason, or a suspected defect marked as a new issue candidate}

## Not covered

- {Skipped case or item, and the missing prerequisite}
