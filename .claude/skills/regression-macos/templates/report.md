# Branch Output {plugin_version} regression test (macOS)

- Work folder: {path of `<work>`}
- Build: {commit or bundle path}, `Contents/MacOS/osi-branch-output` SHA-256 {hash}
- OBS: {version}: {asset name}, verified by {digest / size} (one line per version)
- Machine: {macOS version, chip, available hardware encoders, Xcode used for the build}
- Date: {YYYY-MM-DD}

## Summary

{Counts of PASS / FAIL / SKIP / BLOCKED per version, and the conclusion in two or three sentences.}

## Results

| ID | Case | 32.2 | 31.1 | 30.1.2 | Notes |
|---|---|---|---|---|---|
| R01 | Startup and new filter | {result} | {result} | {result} | {short note} |

## Failures

### {ID} on {version}: {one-line summary}

- Operation: {what was done}
- Expected: {Pass item}
- Actual: {what happened}
- Evidence: {files in this folder: log excerpts, ffprobe output, crash report}
- Reproduced on retry: {yes / no}
- Related issue: {#N or new}

## Observations

Behavior worth noting that did not fail a case, grouped as below. Leave out an empty group.

- Environment
  - {A problem of the machine or the operation, and how it was worked around}
- OBS
  - {Behavior of OBS itself, and the evidence that OBS shows it without the plugin}
- Plugin, known issues
  - {Behavior matching an existing issue, with the case ID and the issue number}
- Plugin, no issue
  - {Behavior not judged a defect, with the case ID and the reason}

## Not covered

- {Skipped case or item, and the missing prerequisite}
