---
name: regression
description: Pre-release regression test of the Branch Output plugin on portable OBS Studio builds downloaded from the official releases (32.2 latest, 31.1 final, 30.1.2), driving OBS through its GUI with computer use and verifying outputs with ffprobe and OBS logs. Use before a release or when the user asks for a regression or run-through test of Branch Output.
---

# Branch Output Regression Test

Run the cases in `.claude/skills/regression/cases.md` on each target OBS version and record the results in a report, confirming before a release that the main path of every feature and the listed failure-prone edge cases still work.

## Input

The user may specify the following; interpret `$ARGUMENTS` accordingly.

- A subset of the target versions (default all three).
- The plugin build under test: a folder in install layout (`obs-plugins/64bit/osi-branch-output.dll`, `data/obs-plugins/osi-branch-output/`). When omitted, build the current checkout with `build.ps1` and use `release/Package`.
- Case IDs to run (default all).
- An existing report to resume: reuse the work folder and the build recorded in the report, and run only the cells that have no result. When the work folder is gone, redo Step 1 at the recorded work folder path with the OBS releases recorded in the report, without creating a new report, and before a case that relies on an earlier case's setup (R14 on R13's hotkey assignments), redo that setup.

## Layout

`{YYYYMMDD-HHmmss}` is the local time at which the run starts; both folders below use the same value. `{version}` is the full release version (for example `32.2.2`).

- `<work>` = `.claude/tmp/regression-{YYYYMMDD-HHmmss}/`: the working folder of the run.
  - `<work>/obs-{version}/`: the extracted OBS of each version. `<root>` below is the folder in it that contains `bin/64bit/obs64.exe`.
  - `<work>/media/`: test media shared by all versions.
  - `<root>/work/`: recordings and receiver captures of that version.
- `.claude/tmp/regression-report/{plugin-version}-{YYYYMMDD-HHmmss}/`: the report and its evidence. It stays when `<work>` is deleted.

## Ground rules

- Start every OBS process, including relaunches within a case, from a shell as `<root>/bin/64bit/obs64.exe --portable --disable-updater` with the working directory `<root>/bin/64bit` (add `--profile` / `--collection` to select the fixture).
  - Never start OBS from the Start menu or a desktop or taskbar shortcut: they start the installed OBS.
  - Never omit `--portable`: without it, even the extracted OBS reads and rewrites the installed OBS's settings under `%APPDATA%\obs-studio`.
  - Never use computer use's `open_application` on any `obs64.exe`, including `<root>/bin/64bit/obs64.exe`, and not to bring a running instance to the front: it starts the executable without arguments, so without `--portable`. Bring OBS to the front with user32 `SetForegroundWindow` from PowerShell.
  - After each launch, confirm `Portable mode: true` in the new log under `<root>/config/obs-studio/logs/`. If it is missing, quit OBS at once, stop the run, and report to the user, offering to restore `%APPDATA%\obs-studio` from the Step 1 backup.
- On the first launch of an instance, decline the Auto-Configuration Wizard (its bandwidth test streams to external services).
- Run one OBS instance at a time.
- Quit OBS from its GUI (File → Exit) so the shutdown log is written. Kill the process only when it hangs, and record the hang as a failure. After an unclean exit, OBS offers Safe Mode at the next launch; launch normally unless the case calls for Safe Mode.
- Stream only to local receivers on 127.0.0.1. Never stream to an external service.
- Keep each recording or stream to about 10–20 s unless the case says otherwise.

## Tools

- GitHub releases: `gh` against `obsproject/obs-studio` (release list, asset names and digests, download).
- GUI: computer use.
  - Required for adding Branch Output filters (one created with obs-websocket `CreateSourceFilter` never starts its outputs, because its timer lives on a thread without an event loop), the filter properties (including the filter's own Apply button), the "Branch Output Status" dock, Settings → Hotkeys, Tools → Scripts, hotkey presses, and Undo together with the deletion it reverts (Undo reverts only GUI operations).
  - Access is granted per executable path: request access to `obs64.exe` while the instance of each version runs.
  - Take a fresh screenshot before each click in the filter properties: the dialog re-lays out when a setting changes (notably on 31.1), and earlier coordinates can hit another control.
  - Widen the dock's Status column (Split / Pause / Unpause / Add chapter / Save) and last column (Reset) before clicking a row's buttons: at the default widths clicks miss them.
  - Avoid typing: typed text passes through the IME and can be altered, and the file dialog's folder field rejects typing. Set names through obs-websocket, and paths in the config files under `<root>/config/obs-studio/` while OBS is closed.
- obs-websocket: when a client is available, allowed for any other change or observation it supports (for example main streaming / recording / replay buffer / virtual camera, scene switching, Studio Mode, source and filter settings, `GetSourceActive`, `GetHotkeyList`).
  - Enable its server while OBS is closed: `plugin_config/obs-websocket/config.json` on 31.1 and later, the `[OBSWebSocket]` section of `global.ini` on 30.1.2, both under `<root>/config/obs-studio/`.
  - On 30.1.2, `TriggerHotkeyByName` on one half of a hotkey pair (Enable / Disable, Pause / Unpause) desyncs the pair: press the key or use the dock instead.
  - The per-output enabled states (the dock checkboxes, also toggled by hotkeys) are runtime state: writing them into the filter settings through obs-websocket has no effect. Change them with the dock or hotkeys (key presses, or `TriggerHotkeyByName` within the 30.1.2 limit above).
- Local receivers: ffmpeg listening for RTMP (`-listen 1` accepts one connection and exits when it ends; restart it for each connection) and SRT.
- Output checks: ffprobe / ffmpeg for resolution, frame rate, duration, streams, chapters, pixel colors of a frame, and audio level and frequency.
- Logs: the newest file in `<root>/config/obs-studio/logs/`. Plugin lines start with `[osi-branch-output]`; shutdown writes `Number of memory leaks: N`. Crash dumps go to `<root>/config/obs-studio/crashes/`.

## Step 1 — Prepare

1. Set `{YYYYMMDD-HHmmss}` and create `<work>`. When `%APPDATA%\obs-studio` exists, back it up to `<work>/appdata-obs-studio/`, leaving out the browser cache `plugin_config/obs-browser/`.
2. For each target version, pick the release (newest non-prerelease 32.2.x, newest non-prerelease 31.1.x, 30.1.2) and download its Windows x64 zip into `<work>`, not the installer or the PDBs: `OBS-Studio-{version}-Windows-x64.zip`, or `OBS-Studio-{version}.zip` on 30.1.2. When the release lists a SHA-256 digest for the asset, verify the download against it. Extract it to `<work>/obs-{version}/`.
3. Obtain the build under test and deploy it to every instance: put `osi-branch-output.dll` and `.pdb` in `<root>/obs-plugins/64bit/`, and the `data/obs-plugins/osi-branch-output/` folder in `<root>/data/obs-plugins/`. Record the DLL's SHA-256.
4. Launch each instance once and read `{plugin-version}` from `[osi-branch-output] Plugin loaded successfully (version {plugin-version})`. If the plugin does not load, or the version is not that of the build under test, stop and report to the user. After quitting, set `MaxLogs` under `[General]` in `<root>/config/obs-studio/global.ini` to 100: OBS deletes the oldest logs beyond it (default 10).
5. Create the report at `.claude/tmp/regression-report/{plugin-version}-{YYYYMMDD-HHmmss}/report.md` from the template `.claude/skills/regression/templates/report.md` (read it to learn the report skeleton), filling in the work folder, the build, and the OBS downloads. Keep evidence files (ffprobe output, log excerpts) in the same folder. Write the report in the language the user converses in.
6. Build the fixture described in `cases.md` on each instance.

## Step 2 — Run the cases

- Run 32.2 first, then 31.1, then 30.1.2, each with the selected cases in its scope (`cases.md`); mark cells out of scope or not selected N/A.
- Write each result into the report as soon as its case finishes.
- Result values: PASS, FAIL, SKIP (a prerequisite is missing on this machine; give the reason), BLOCKED (an earlier failure prevents the case; name it), N/A.
- When a missing prerequisite rules out only some items of a case, judge the case on the remaining items, and list the skipped items in the Notes column and under "Not covered".
- A case passes only when every Pass and Edge item holds, no new `[osi-branch-output]` line reports an unexpected failure, and OBS did not crash or hang. Lines caused by a failure the case provokes on purpose (a stopped receiver, a missing source file, an SRT listener without a caller) are expected.
- On a failure:
  - Record the operation, the expected and actual results, and the evidence.
  - Retry the failing operation once and record whether it reproduced.
  - Search the GitHub issues (`gh issue list --state all --search ...`) for a matching report and link it, or mark the failure as new.
  - Continue with the next case. After a crash, copy the crash dump to the report folder, relaunch OBS, and continue.
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
- For each version, the counts of PASS / FAIL / SKIP / BLOCKED.
- Each failure in one line: case ID, version, whether it reproduced, and the related issue or "new".
- What this machine could not cover (skipped cases and the missing prerequisites).
