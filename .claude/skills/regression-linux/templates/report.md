# Branch Output {plugin-version} regression test (Linux)

- Work folder: {path of `<work>`}
- Build: {commit or plugin folder}, `osi-branch-output.so` SHA-256 {hash}
- OBS: {version}, built from `OBS-Studio-{version}-Sources.tar.gz` (SHA-256 {digest}) into {path of `<obs>/install`}
- Machine: {distribution and version, kernel, CPU, GPU, hardware encoders offered, compiler used for the builds}
- Date: {YYYY-MM-DD}

## Summary

{Counts of PASS / FAIL / SKIP / BLOCKED, and the conclusion in two or three sentences.}

## Results

| ID | Case | Result | Notes |
|---|---|---|---|
| R01 | Startup and new filter | {result} | {short note} |

## Failures

### {ID}: {one-line summary}

- Operation: {what was done}
- Expected: {Pass item}
- Actual: {what happened}
- Evidence: {files in this folder: log excerpts, ffprobe output, `coredumpctl info` output}
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
