# Branch Output {plugin_version} regression test

- Work folder: {path of `<work>`}
- Build: {commit or package path}, DLL SHA-256 {hash}
- OBS: {version}: {asset name}, digest {verified / not published} (one line per version)
- Machine: {OS, CPU, GPU, available hardware encoders}
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
- Evidence: {files in this folder: log excerpts, ffprobe output, crash dump}
- Reproduced on retry: {yes / no}
- Related issue: {#N or new}

## Observations

- {Behavior worth noting that did not fail a case}

## Not covered

- {Skipped case or item, and the missing prerequisite}
