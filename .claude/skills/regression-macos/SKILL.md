---
name: regression-macos
description: Pre-release regression test of the Branch Output plugin on macOS, on OBS Studio app bundles extracted from the official releases (32.2 latest, 31.1 final, 30.1.2) and run with isolated config folders, driving OBS through its GUI with computer use and verifying outputs with ffprobe and OBS logs. Use before a release or when the user asks for a regression or run-through test of Branch Output on macOS.
---

# Branch Output Regression Test (macOS)

Run the cases in `.claude/skills/regression-macos/cases.md` on each target OBS version and record the results in a report, confirming before a release that the main path of every feature and the listed failure-prone edge cases still work on macOS.

## Input

The user may specify the following; interpret `$ARGUMENTS` accordingly.

- A subset of the target versions (default all three).
- The plugin build under test: an `osi-branch-output.plugin` bundle (from a `.pkg`, extract the bundle with `pkgutil --expand-full`). When omitted, build the current checkout (Step 1).
- Case IDs to run (default all).
- An existing report to resume: reuse the work folder and the build recorded in the report, and run only the cells that have no result. When the work folder is gone, redo Step 1 at the recorded work folder path with the OBS releases recorded in the report, without creating a new report, and before a case that relies on an earlier case's setup (R14 on R13's hotkey assignments), redo that setup.

## Layout

`{YYYYMMDD-HHmmss}` is the local time at which the run starts; both folders below use the same value. `{version}` is the full release version (for example `32.2.2`).

- `<work>` = `.claude/tmp/regression-macos-{YYYYMMDD-HHmmss}/`: the working folder of the run.
  - `<root>` = `<work>/obs-{version}/`: holds that version's `OBS.app` and its isolated home `<root>/home/`.
  - `<config>` = `<root>/home/Library/Application Support/obs-studio/`: that version's OBS config (logs, profiles, scene collections, plugins, plugin configs).
  - `<work>/media/`: test media shared by all versions.
  - `<root>/work/`: recordings and receiver captures of that version.
- `.claude/tmp/regression-report/{plugin-version}-macos-{YYYYMMDD-HHmmss}/`: the report and its evidence. It stays when `<work>` is deleted.

## Ground rules

- Start every OBS process, including relaunches within a case, from a shell in the background as `CFFIXED_USER_HOME=<root>/home <root>/OBS.app/Contents/MacOS/OBS --disable-updater` (add `--profile` / `--collection` to select the fixture, and `--safe-mode` when a case calls for Safe Mode), and note its PID. The only exception is R10's Virtual Cam value, launched from `/Applications/OBS.app` as `cases.md` describes.
  - macOS OBS has no portable mode: `--portable` is ignored, and without `CFFIXED_USER_HOME` the instance reads and rewrites the user's own config in `~/Library/Application Support/obs-studio/`. `HOME` does not redirect it.
  - Never start OBS through LaunchServices: computer use's `open_application`, `open`, Finder, the Dock, Spotlight, or AppleScript `tell application "OBS"` / `tell application id`. They resolve the bundle ID `com.obsproject.obs-studio`, which the installed `/Applications/OBS.app` shares, and never pass the environment variable.
  - After each launch, confirm that the new log appeared in `<config>/logs/` and that nothing in `~/Library/Application Support/obs-studio/` is newer than the launch. Otherwise quit OBS at once, stop the run, and report to the user, offering to restore that folder from the Step 1 backup.
- Keep the installed OBS closed throughout the run (ask the user to quit it when it runs), except for the launch above for R10's Virtual Cam value, and run one OBS instance at a time: OBS treats another running process with its bundle ID as "already running".
- After each launch, bring OBS to the front once: `osascript -e 'tell application "System Events" to set frontmost of (first process whose unix id is <PID>) to true'`. Until then its menu bar has only the app menu. Use the same command whenever OBS must be frontmost.
- On the first launch of an instance, the app permission check dialog appears: press Continue without granting anything. The Auto-Configuration Wizard opens next: press Cancel (its bandwidth test streams to external services).
- Grant OBS no macOS permission (camera, microphone, screen recording, input monitoring, accessibility); the cases need none.
- Quit OBS from its app menu (OBS → Quit OBS) so the shutdown log is written. Kill the process only when it hangs, and record the hang as a failure. After an unclean exit, OBS offers Safe Mode at the next launch; launch normally unless the case calls for Safe Mode.
- Stream only to local receivers on 127.0.0.1. Never stream to an external service.
- Keep each recording or stream to about 10–20 s unless the case says otherwise.

## Tools

- GitHub releases: `gh` against `obsproject/obs-studio` (release list, asset names, sizes and digests, download).
- GUI: computer use.
  - Required for adding Branch Output filters (one created with obs-websocket `CreateSourceFilter` never starts its outputs, because its timer lives on a thread without an event loop), the filter properties (including the filter's own Apply button), the "Branch Output Status" dock, Settings → Hotkeys, Tools → Scripts, hotkey presses, and Undo together with the deletion it reverts (Undo reverts only GUI operations).
  - Request access to the bundle ID `com.obsproject.obs-studio`; one grant covers every version. Request Finder as well for R12, and check the Finder window with computer use only: querying Finder with AppleScript (`tell application "Finder"`) makes macOS ask the user to allow automation of Finder, the Apple Event times out, and the prompt stays on screen over OBS.
  - Prefer the background `app_*` tools. They cannot open context menus; open a source's filters with the Filters button of the toolbar below the preview (shown while the source is selected) instead of the context menu. Use the full-screen tools with OBS frontmost only for what `app_*` refuses.
  - Settings is OBS → Preferences in the app menu.
  - Take a fresh screenshot before each click in the filter properties: the dialog re-lays out when a setting changes (notably on 31.1), and earlier coordinates can hit another control.
  - Widen the dock's Status column (Split / Pause / Unpause / Add chapter / Save) and last column (Reset) before clicking a row's buttons: at the default widths clicks miss them.
  - Avoid typing: typed text passes through the IME and can be altered. Set names through obs-websocket, and paths in the config files under `<config>` while OBS is closed.
  - Hotkeys: macOS modifiers are Control, Option, Shift, and Command. Without the input monitoring permission OBS registers no global hotkeys (it logs `No event permissions, could not add global hotkeys`), so a key acts only while OBS is frontmost with its main window focused. Confirm with the first key press that keys reach OBS; if none do, use `TriggerHotkeyByName` (within the 30.1.2 limit below) and note it in the report.
- obs-websocket: when a client is available, allowed for any other change or observation it supports (for example main streaming / recording / replay buffer, scene switching, Studio Mode, source and filter settings, `GetSourceActive`, `GetHotkeyList`).
  - Enable its server while OBS is closed: `<config>/plugin_config/obs-websocket/config.json` on 31.1 and later, the `[OBSWebSocket]` section of `<config>/global.ini` on 30.1.2.
  - On 30.1.2, `TriggerHotkeyByName` on one half of a hotkey pair (Enable / Disable, Pause / Unpause) desyncs the pair: press the key or use the dock instead.
  - The per-output enabled states (the dock checkboxes, also toggled by hotkeys) are runtime state: writing them into the filter settings through obs-websocket has no effect. Change them with the dock or hotkeys (key presses, or `TriggerHotkeyByName` within the 30.1.2 limit above).
- Local receivers: ffmpeg listening for RTMP (`-listen 1` accepts one connection and exits when it ends; restart it for each connection) and SRT. When `ffmpeg -protocols` lists no `srt`, SKIP the SRT items.
- Output checks: ffprobe / ffmpeg for resolution, frame rate, duration, streams, chapters, pixel colors of a frame, and audio level and frequency.
- Logs: the newest file in `<config>/logs/`. Plugin lines start with `[osi-branch-output]`; shutdown writes `Number of memory leaks: N`. `Failed to enumerate diagnostics reports directory` at startup is expected. macOS writes crash reports to `~/Library/Logs/DiagnosticReports/OBS*.ips` in the user's real home, not under `<config>`.
- `lsof -p <PID>` shows which plugin binary a running instance loaded.

## Step 1 — Prepare

1. Set `{YYYYMMDD-HHmmss}` and create `<work>`. When `~/Library/Application Support/obs-studio` exists, back it up to `<work>/appdata-obs-studio/`, leaving out the browser cache `plugin_config/obs-browser/`.
2. For each target version, pick the release (newest non-prerelease 32.2.x, newest non-prerelease 31.1.x, 30.1.2) and download `OBS-Studio-{version}-macOS-Apple.dmg` on Apple silicon or `OBS-Studio-{version}-macOS-Intel.dmg` on Intel into `<work>`, not the dSYMs. Verify it against the release's SHA-256 digest when one is listed, otherwise against the listed size. Mount it read-only without Finder (`hdiutil attach -nobrowse -readonly -mountpoint …`), copy `OBS.app` to `<root>/` with `ditto`, detach, and confirm `codesign --verify --deep --strict` passes. Never add or change files inside `OBS.app`: that breaks its signature.
3. Obtain the build under test. To build the current checkout, use the `macos` presets (`cmake --preset macos`, then `cmake --build --preset macos`; the bundle is `build_macos/Release/osi-branch-output.plugin`) with Xcode 16.4 or earlier: the project does not build with Xcode 26. When `xcode-select -p` points to a newer Xcode, set `DEVELOPER_DIR` to an older Xcode's `Contents/Developer`; when none is installed, stop and ask the user. Deploy the bundle to every instance with `ditto` as `<config>/plugins/osi-branch-output.plugin`, and record the SHA-256 of its `Contents/MacOS/osi-branch-output`.
4. Launch each instance once and read `{plugin-version}` from `[osi-branch-output] Plugin loaded successfully (version {plugin-version})`. With `lsof`, confirm that the loaded `osi-branch-output` binary is the one under `<config>/plugins/`: the user's own OBS config may hold another copy of the plugin. If the plugin does not load, is loaded from elsewhere, or its version is not that of the build under test, stop and report to the user. After quitting, set `MaxLogs` under `[General]` in `<config>/global.ini` to 100: OBS deletes the oldest logs beyond it (default 10).
5. Create the report at `.claude/tmp/regression-report/{plugin-version}-macos-{YYYYMMDD-HHmmss}/report.md` from the template `.claude/skills/regression-macos/templates/report.md` (read it to learn the report skeleton), filling in the work folder, the build, and the OBS downloads. Keep evidence files (ffprobe output, log excerpts) in the same folder. Write the report in the language the user converses in.
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
  - Continue with the next case. After a crash, copy the new crash report from `~/Library/Logs/DiagnosticReports/` to the report folder, relaunch OBS, and continue.
- Stop and ask the user when the environment blocks most of the remaining cases (a download fails, OBS does not start, the receivers cannot run, the fixture cannot be built).

## Step 3 — Clean up

Skip this step when the run stopped before every selected case finished, so that it can be resumed.

1. Quit OBS and the receivers.
2. Copy each instance's `<config>/logs/` to the report folder.
3. Delete `<work>`. When recursive deletion is not permitted in this session, give its path to the user to delete instead.

## Step 4 — Report

Tell the user:

- The report path.
- The OBS versions tested.
- For each version, the counts of PASS / FAIL / SKIP / BLOCKED.
- Each failure in one line: case ID, version, whether it reproduced, and the related issue or "new".
- What this machine could not cover (skipped cases and the missing prerequisites).
