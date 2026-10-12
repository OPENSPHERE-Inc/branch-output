---
name: regression-next
description: Regression test of the Branch Output plugin on Windows against only the newest OBS Studio release, beta and release candidate pre-releases included, running the regression skill's cases on a portable build of that release. Use when the user asks for a regression or run-through test of Branch Output on the latest OBS, an OBS beta, or an OBS release candidate on Windows.
---

# Branch Output Regression Test on the Next OBS

Run the regression skill's cases on one OBS version, the newest release including pre-releases. Read `.claude/skills/regression/SKILL.md` and follow it with the differences below. The `cases.md` and report template it names are in `.claude/skills/regression/`.

## Target release

- The release of `obsproject/obs-studio` with the highest version, pre-releases (beta, rc) included. A pre-release ranks below the final release of the same version (`33.0.0-rc1` < `33.0.0`). Compare versions, not publication dates or GitHub's "Latest" label: the label skips pre-releases, and a patch release of an older series can be published after a newer pre-release.
- `{version}` is its tag (for example `33.0.0-rc1`). Download `OBS-Studio-{version}-Windows-x64.zip`.
- Wherever `cases.md` or the regression skill distinguishes OBS versions (case scope, items limited to some versions, config file locations), treat the target as 32.2, the newest version they name. Every case is then in scope.
- When an operation relies on an OBS setting, file, or UI element that the target has moved or removed, perform it through the replacement and note the change under Observations → OBS. When there is no replacement, mark the affected items SKIP with the reason.

## Differences from the regression skill

- Input: no version selection. When the user also asks for other OBS versions, run only the target and tell the user that the regression skill covers the others. When resuming a report, use the release recorded in it, even if a newer one has been published since.
- Layout: `<work>` = `.claude/tmp/regression-next-{YYYYMMDD-HHmmss}/`; the report folder, also in Step 1.5, is `.claude/tmp/regression-report/{plugin-version}-obs-{version}-{YYYYMMDD-HHmmss}/`.
- Step 1.2: download only the target release.
- Step 1.5: the Results table has one result column headed `{version}` in place of the 32.2 / 31.1 / 30.1.2 columns.
