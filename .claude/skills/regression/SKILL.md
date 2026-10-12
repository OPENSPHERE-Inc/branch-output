---
name: regression
description: Pre-release regression test of the Branch Output plugin on Windows, on portable OBS Studio builds downloaded from the official releases (32.2 latest, 31.1 final, 30.1.2), driving OBS through its GUI with computer use and verifying outputs with ffprobe and OBS logs. Use before a release or when the user asks for a regression or run-through test of Branch Output on Windows.
---

# Branch Output Regression Test

Run the cases in `.claude/skills/regression/cases.md` on each target OBS version and record the results in a report, confirming before a release that the main path of every feature and the listed failure-prone edge cases still work.

## Input

The user may specify the following; interpret `$ARGUMENTS` accordingly.

- A subset of the target versions (default all three).
- The plugin build under test: a folder in install layout (`obs-plugins/64bit/osi-branch-output.dll`, `data/obs-plugins/osi-branch-output/`). When omitted, build the current checkout with `build.ps1` and use `release/Package`.
- Case IDs to run (default all).
- An existing report to resume: reuse the work folder and the build recorded in the report, and run only the cells that have no result. Before running them, request computer-use access as in Step 1.4 for each version that has cells left. When the work folder is gone, redo Step 1 at the recorded work folder path with the OBS releases recorded in the report, without creating a new report, and before a case that relies on an earlier case's setup (R14 on R13's hotkey assignments, the M cases on M01's Program Output), redo that setup.

## Layout

`{YYYYMMDD-HHmmss}` is the local time at which the run starts; both folders below use the same value. `{version}` is the full release version (for example `32.2.2`).

- `<work>` = `.claude/tmp/regression-{YYYYMMDD-HHmmss}/`: the working folder of the run.
  - `<work>/obs-{version}/`: the extracted OBS of each version. `<root>` below is the folder in it that contains `bin/64bit/obs64.exe`.
  - `<work>/media/`: test media shared by all versions.
  - `<root>/work/`: recordings and receiver captures of that version.
- `.claude/tmp/regression-report/{plugin-version}-{YYYYMMDD-HHmmss}/`: the report and its evidence. It stays when `<work>` is deleted.

## Ground rules

- Start every OBS process, including relaunches within a case, from a shell as `<root>/bin/64bit/obs64.exe --portable --disable-updater` with the working directory `<root>/bin/64bit` (add `--profile` / `--collection` to select the fixture), which `launch.ps1` does.
  - Never start OBS from the Start menu or a desktop or taskbar shortcut: they start the installed OBS.
  - Never omit `--portable`: without it, even the extracted OBS reads and rewrites the installed OBS's settings under `%APPDATA%\obs-studio`.
  - Never use computer use's `open_application` on any `obs64.exe`, including `<root>/bin/64bit/obs64.exe`, and not to bring a running instance to the front: it starts the executable without arguments, so without `--portable`. Bring OBS to the front with `window.ps1 front -Root <root>`.
  - After each launch, confirm `Portable mode: true` in the new log under `<root>/config/obs-studio/logs/`. If it is missing, quit OBS at once, stop the run, and report to the user, offering to restore `%APPDATA%\obs-studio` from the Step 1 backup.
- On the first launch of an instance, decline the Auto-Configuration Wizard (its bandwidth test streams to external services).
- Run one OBS instance at a time.
- Quit OBS from its GUI (File → Exit, or the main window's close button through `quit.ps1 -Root <root>`) so the shutdown log is written. When `quit.ps1` reports the process still running, take a screenshot: a dialog may be waiting for an answer. Kill the process only when it hangs, and record the hang as a failure. After an unclean exit, OBS offers Safe Mode at the next launch; launch normally unless the case calls for Safe Mode.
- Stream only to local receivers on 127.0.0.1. Never stream to an external service.
- Keep each recording or stream to about 10–20 s unless the case says otherwise.

## Tools

- GitHub releases: `gh` against `obsproject/obs-studio` (release list, asset names and digests, download).
- Helper scripts. Each documents its usage at the top of the file. Run the Python ones with `py -3`; `obsws.py` needs the `obsws-python` package. Do not edit them during a run: write any other helper under `<work>/scripts/`, and report a defect of a bundled script to the user.
  - `.claude/scripts/regression/`: `media.py` (test media), `fixture.py` (the `cases.md` fixture, written while OBS is closed), `obsws.py` (obs-websocket requests), `obs-config.py` (scripts and saved hotkey bindings in the scene collection of a closed instance), `probe.py` (recording checks).
  - `.claude/skills/regression/scripts/` (PowerShell, `pwsh -NoProfile -File`): `launch.ps1`, `window.ps1` (front, maximize, resize, list), `quit.ps1`, `receiver.ps1` (named ffmpeg receivers, with `<work>/rx/` as their state folder).
- GUI: computer use.
  - Required for adding Branch Output filters (one created with obs-websocket `CreateSourceFilter` never starts its outputs, because its timer lives on a thread without an event loop), the filter properties (including the filter's own Apply button), a Program Output's properties dialog where a case checks it, the "Branch Output Status" dock (including its "+" menu and its rows' context menus), Settings → Hotkeys, Tools → Scripts, hotkey presses, and Undo together with the deletion it reverts (Undo reverts only GUI operations).
  - Access is granted per executable path and requested while that instance runs.
  - Take a fresh screenshot before each click in the filter properties and in a Program Output's properties dialog, and again after any obs-websocket change while the filter properties are open: the dialog re-lays out when a setting changes (notably on 31.1), and an obs-websocket change also scrolls the filter properties back to the top, so earlier coordinates can hit another control.
  - Widen the dock's Status column (Split / Pause / Unpause / Add chapter / Save) and last column (Reset) before clicking a row's buttons: at the default widths clicks miss them.
  - Avoid typing: typed text passes through the IME and can be altered, and the file dialog's folder field rejects typing. Set names, numeric values, and filter settings that hold a path (such as the Save Path) through obs-websocket, and other paths in the config files under `<root>/config/obs-studio/` while OBS is closed. An obs-websocket change to a filter's settings applies at once, like Apply, together with any unapplied edit in its open properties: in a step that must stay unapplied until Apply (R15), change the value in the properties instead (for the resolution, pick a dropdown entry such as "Half of source (50%)") and send no obs-websocket settings change to that filter until the step's Apply. The exceptions are the dock's rename dialog for Program Outputs, where `cases.md` M11 and M12 paste the name from the clipboard, and OBS's profile rename dialog, where M09 and M14 do the same.
- obs-websocket: allowed for any other change or observation it supports (for example main streaming / recording / replay buffer / virtual camera, scene switching, Studio Mode, source and filter settings, `GetSourceActive`, `GetHotkeyList`). `fixture.py` enables its server on the port given with `--ws-port`, with a password it generates on each run: use a port other than 4455, which the user's own OBS may hold. Pass `--config <root>/config/obs-studio` to every `obsws.py` call: it reads the port and the password from there.
  - In Studio Mode, set the Preview scene only after the transition to the Program scene has ended: the transition ends by swapping the old Program into the Preview. `obsws.py studio` does both in this order and waits for the transition.
  - On 30.1.2, `TriggerHotkeyByName` on one half of a hotkey pair (Enable / Disable, Pause / Unpause) desyncs the pair: press the key or use the dock instead.
  - The per-output enabled states (the dock checkboxes, also toggled by hotkeys) are runtime state: writing them into the filter settings through obs-websocket has no effect. Change them with the dock or hotkeys (key presses, or `TriggerHotkeyByName` within the 30.1.2 limit above).
- Local receivers: ffmpeg listening for RTMP (`-listen 1` accepts one connection and exits when it ends; restart it for each connection) and SRT, run with `receiver.ps1`.
- Output checks: ffprobe / ffmpeg for resolution, frame rate, duration, streams, chapters, pixel colors of a frame, and audio level and frequency (`probe.py` covers these).
- Logs: the newest file in `<root>/config/obs-studio/logs/`. Plugin lines start with `[osi-branch-output]`; shutdown writes `Number of memory leaks: N`. Crash dumps go to `<root>/config/obs-studio/crashes/`.

## Step 1 — Prepare

1. Set `{YYYYMMDD-HHmmss}` and create `<work>`. When `%APPDATA%\obs-studio` exists, back it up to `<work>/appdata-obs-studio/`, leaving out the browser cache `plugin_config/obs-browser/`.
2. For each target version, pick the release (newest non-prerelease 32.2.x, newest non-prerelease 31.1.x, 30.1.2) and download its Windows x64 zip into `<work>`, not the installer or the PDBs: `OBS-Studio-{version}-Windows-x64.zip`, or `OBS-Studio-{version}.zip` on 30.1.2. When the release lists a SHA-256 digest for the asset, verify the download against it. Extract it to `<work>/obs-{version}/`.
3. Obtain the build under test and deploy it to every instance: put `osi-branch-output.dll` and `.pdb` in `<root>/obs-plugins/64bit/`, and the `data/obs-plugins/osi-branch-output/` folder in `<root>/data/obs-plugins/`. Record the DLL's SHA-256.
4. Launch each instance once. While it runs, request computer-use access to its `obs64.exe`, so that the rest of the run needs no user approval. Read `{plugin-version}` from `[osi-branch-output] Plugin loaded successfully (version {plugin-version})`. If the plugin does not load, or the version is not that of the build under test, stop and report to the user.
5. Create the report at `.claude/tmp/regression-report/{plugin-version}-{YYYYMMDD-HHmmss}/report.md` from the template `.claude/skills/regression/templates/report.md` (read it to learn the report skeleton), filling in the work folder, the build, and the OBS downloads. Keep evidence files (ffprobe output, log excerpts) in the same folder. Write the report in the language the user converses in.
6. Build the fixture described in `cases.md` on each instance: generate the media once with `media.py <work>/media`, then, with the instance closed, run `fixture.py --config <root>/config/obs-studio --media <work>/media --rec <root>/work --obs-version {version} --ws-port <port>` (the obs-websocket port under Tools).

## Step 2 — Run the cases

- Run 32.2 first, then 31.1, then 30.1.2, each with the selected cases in its scope, in the order of `cases.md` (R01–R19, then M01–M14); mark cells out of scope or not selected N/A.
- Write each result into the report as soon as its case finishes.
- Result values: PASS, FAIL, SKIP (a prerequisite is missing on this machine; give the reason), BLOCKED (an earlier failure prevents the case; name it), N/A.
- When a missing prerequisite rules out only some items of a case, judge the case on the remaining items, and list the skipped items in the Notes column and under "Not covered".
- A case passes only when every Pass and Edge item holds, no new `[osi-branch-output]` line reports an unexpected failure, and OBS did not crash or hang. Lines caused by a failure the case provokes on purpose (a stopped receiver, a missing source file, an SRT listener without a caller) are expected.
- On a failure:
  - Record the operation, the expected and actual results, and the evidence.
  - Retry the failing operation once and record whether it reproduced.
  - Search the GitHub issues (`gh issue list --state all --search ...`) for a matching report and link it, or mark the failure as new. A FAIL that matches an open issue is a known-issue FAIL; count it apart from new FAILs. A FAIL that matches only closed issues is a regression of a fixed issue: count it as new and write `new (regression of #N)` as its related issue.
  - Continue with the next case. After a crash, copy the crash dump to the report folder, relaunch OBS, and continue.
- Search the issues the same way for each plugin behavior noted under Observations. One that matches an open issue goes under "Plugin, known issues" with the issue number; any other goes under "Plugin, no issue", with `regression of #N` when it matches a closed issue.
- Stop and ask the user when the environment blocks most of the remaining cases (a download fails, OBS does not start, the receivers cannot run, the fixture cannot be built).

## Step 3 — Clean up

Skip this step when the run stopped before every selected case finished, so that it can be resumed.

1. Quit OBS and the receivers.
2. Copy each instance's `<root>/config/obs-studio/logs/` to the report folder.
3. Delete `<work>`.

## Step 4 — Report

Tell the user:

- The report path.
- The OBS versions tested.
- For each version, the counts of PASS / FAIL / SKIP / BLOCKED, with FAIL split into new and known-issue FAILs.
- Each failure in one line: case ID, version, whether it reproduced, and the related issue (`#N`, `new`, or `new (regression of #N)`).
- What this machine could not cover (skipped cases and the missing prerequisites).
