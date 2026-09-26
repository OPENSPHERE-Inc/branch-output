---
name: regression-linux
description: Pre-release regression test of the Branch Output plugin on Linux, on the OBS Studio release matching the OBS installed on the machine, built from the release's source tarball into a temporary folder and run on a virtual X display (Xvfb) with an isolated config, driving OBS through its GUI with xdotool and screenshots and verifying outputs with ffprobe and OBS logs. Use before a release or when the user asks for a regression or run-through test of Branch Output on Linux.
---

# Branch Output Regression Test (Linux)

Run the cases in `.claude/skills/regression-linux/cases.md` and record the results in a report, confirming before a release that the main path of every feature and the listed failure-prone edge cases still work on Linux.

## Input

The user may specify the following; interpret `$ARGUMENTS` accordingly.

- The plugin build under test: a folder in install layout (`lib64/obs-plugins/osi-branch-output.so`, `share/obs/obs-plugins/osi-branch-output/`). When omitted, build the current checkout (Step 1).
- Case IDs to run (default all).
- An existing report to resume: reuse the work folder, the build, and the OBS version recorded in the report, and run only the cases that have no result. When the work folder is gone, redo Step 1 at the recorded work folder path without creating a new report; if the deployed `osi-branch-output.so` then differs from the recorded SHA-256, stop and ask the user. In that rebuilt folder, before a case that relies on an earlier case's setup (R14 on R13's hotkey assignments), redo that setup.

## Layout

`{YYYYMMDD-HHmmss}` is the local time at which the run starts. `{version}` is the release of the OBS installed on the machine: the version `obs --version` prints, without a suffix such as `-modified` (for example `32.2.2`). When no `obs` is on PATH, ask the user for the version.

- `<obs>` = `.claude/tmp/regression-linux-obs-{version}/`: that release's source tarball, build logs, and install prefix `<obs>/install/`. It is kept across runs.
- `<work>` = `.claude/tmp/regression-linux-{YYYYMMDD-HHmmss}/`: the working folder of the run.
  - `<config>` = `<work>/config/obs-studio/`: the instance's OBS config (logs, profiles, scene collections, plugins, plugin configs).
  - `<work>/media/`: test media. `<work>/rec/`: recordings and receiver captures. `<work>/shots/`: screenshots.
- `.claude/tmp/regression-report/{plugin-version}-linux-{YYYYMMDD-HHmmss}/`: the report and its evidence. It stays when `<work>` is deleted.

## Ground rules

- Leave the installed OBS, its config `~/.config/obs-studio/`, and every OBS source checkout on the machine untouched: build, configure, and run only under `<obs>`, `<work>`, and the report folder. Install no system package; when a build needs one, name it to the user.
- Start every OBS process, including relaunches within a case, with `.claude/skills/regression-linux/scripts/launch-obs.sh <obs>/install <work>` followed by OBS arguments (`--profile` / `--collection` to select the fixture, `--safe-mode` when a case calls for Safe Mode), and note the PID it prints. The instance runs on `:99` with an English UI, its config in `<config>`, and no sound server, and its `xdg-open` only appends its arguments to `<work>/xdg-open.log`.
  - Never start OBS any other way (`obs` on PATH, the desktop launcher): that runs the installed OBS with the user's config.
  - After each launch, confirm that a new log appeared in `<config>/logs/` and that `/proc/<PID>/maps` maps `libobs.so` and every `obs-plugins/` module only from `<obs>/install/`, and `osi-branch-output.so` only from `<config>/plugins/osi-branch-output/bin/64bit/`. Otherwise quit OBS at once, stop the run, and report to the user.
- The user's own OBS may keep running.
- On the first launch of an instance, the Auto-Configuration Wizard opens: press Cancel (its bandwidth test streams to external services).
- Quit OBS from its GUI (File → Exit) so the shutdown log is written. Kill the process only when it hangs, and record the hang as a failure. After an unclean exit, OBS offers Safe Mode at the next launch; launch normally unless the case calls for Safe Mode.
- Stream only to local receivers on 127.0.0.1. Never stream to an external service.
- Keep each recording or stream to about 10–20 s unless the case says otherwise.

## Tools

- Virtual display: `.claude/skills/regression-linux/scripts/display.sh start|stop <work>` runs Xvfb `:99` (1280x800) with openbox. Prefix every X client command with `DISPLAY=:99`: the shell's own `DISPLAY` is the user's desktop.
- GUI: xdotool and screenshots on `:99`.
  - Required for adding Branch Output filters (one created with obs-websocket `CreateSourceFilter` never starts its outputs, because its timer lives on a thread without an event loop), the filter properties (including the filter's own Apply button at the bottom of its scrolled properties), the "Branch Output Status" dock, Settings → Hotkeys, Tools → Scripts, and Undo together with the deletion it reverts (Undo reverts only GUI operations).
  - Screenshot with `import -window root <work>/shots/{name}.png` and Read the file; its pixels are screen coordinates. Click with `xdotool mousemove X Y click 1` (`click 3` for a context menu), scroll with `click 4` / `click 5`, type with `xdotool type`.
  - Take a fresh screenshot before each click: windows move when they reopen, and dialogs re-lay out when a setting changes, so earlier coordinates can hit another control. OBS renders in software (llvmpipe): wait about 1 s after an action before the screenshot; a window that is still black has not painted yet.
  - On a new config the main window opens taller than the screen: move and resize it to 1280x776 at 0,0 with `xdotool windowmove` / `windowsize`. OBS restores that geometry at later launches.
  - Widen the dock's Status column (Split / Pause / Unpause / Add chapter / Save) and last column (Reset) before clicking a row's buttons: at the default widths clicks miss them.
  - Hotkeys: hold the combination for 0.2 s (`xdotool keydown {combo} sleep 0.2 keyup {combo}`). A `xdotool key` tap reaches OBS only while its main window has focus; otherwise OBS samples the key state every 25 ms and misses the tap.
- obs-websocket: when a client is available (for example `obsws-python` in a virtual environment under `<work>`), allowed for any other change or observation it supports (for example main streaming / recording / replay buffer, scene switching, Studio Mode, source and filter settings, `GetSourceActive`, `GetHotkeyList`).
  - Enable its server while OBS is closed, in `<config>/plugin_config/obs-websocket/config.json` (`server_enabled`, `server_port`, `auth_required`, `server_password`). Use a port other than 4455, which the user's own OBS may hold.
  - The per-output enabled states (the dock checkboxes, also toggled by hotkeys) are runtime state: writing them into the filter settings through obs-websocket has no effect. Change them with the dock or hotkeys.
- Local receivers: ffmpeg listening for RTMP (`-listen 1` accepts one connection and exits when it ends; restart it for each connection) and SRT. When `ffmpeg -protocols` lists no `srt`, SKIP the SRT items.
- Output checks: ffprobe / ffmpeg for resolution, frame rate, duration, streams, chapters, pixel colors of a frame, and audio level and frequency.
- Logs: the newest file in `<config>/logs/`. Plugin lines start with `[osi-branch-output]`; shutdown writes `Number of memory leaks: N`. `pulse-input: Unable to get server info` (the instance has no sound server) and `VAAPI: Failed to initialize display` lines are not failures. OBS writes no crash report on Linux: a crash ends the process without the shutdown lines and adds an entry to `coredumpctl list`.

## Step 1 — Prepare

1. Set `{version}` and `{YYYYMMDD-HHmmss}`, and create `<work>`.
2. Run `.claude/skills/regression-linux/scripts/build-obs.sh {version}` in the background: a first build takes several minutes. It builds `<obs>/install/` from the release's `OBS-Studio-{version}-Sources.tar.gz` after verifying the tarball against the release's SHA-256 digest, and reuses a completed build. When it fails, stop and report the error with the log it names.
3. Once `build-obs.sh` has finished, deploy the build under test with `.claude/skills/regression-linux/scripts/build-plugin.sh <obs>/install <work>`, adding the user's folder as a third argument when one was given (without it, the script builds the current checkout against `<obs>/install`). Record the SHA-256 it prints.
4. Start the display, launch OBS, run the launch checks, and read `{plugin-version}` from `[osi-branch-output] Plugin loaded successfully (version {plugin-version})`. If the plugin does not load, or it was built from the current checkout and its version differs from `version` in `buildspec.json`, stop and report to the user. Fit the main window to the screen, then quit, and set `MaxLogs` under `[General]` in `<config>/global.ini` to 100: OBS deletes the oldest logs beyond it (default 10).
5. Create the report at `.claude/tmp/regression-report/{plugin-version}-linux-{YYYYMMDD-HHmmss}/report.md` from the template `.claude/skills/regression-linux/templates/report.md` (read it to learn the report skeleton), filling in the work folder, the build, and the OBS build. Keep evidence files (ffprobe output, log excerpts) in the same folder. Write the report in the language the user converses in.
6. Build the fixture described in `cases.md`.

## Step 2 — Run the cases

- Run the selected cases in ID order; mark the others N/A.
- Write each result into the report as soon as its case finishes.
- Result values: PASS, FAIL, SKIP (a prerequisite is missing on this machine; give the reason), BLOCKED (an earlier failure prevents the case; name it), N/A.
- When a missing prerequisite rules out only some items of a case, judge the case on the remaining items, and list the skipped items in the Notes column and under "Not covered".
- A case passes only when every Pass and Edge item holds, no new `[osi-branch-output]` line reports an unexpected failure, and OBS did not crash or hang. Lines caused by a failure the case provokes on purpose (a stopped receiver, a missing source file, an SRT listener without a caller) are expected.
- On a failure:
  - Record the operation, the expected and actual results, and the evidence.
  - Retry the failing operation once and record whether it reproduced.
  - Search the GitHub issues (`gh issue list --state all --search ...`) for a matching report and link it, or mark the failure as new.
  - Continue with the next case. After a crash, save `coredumpctl info {PID}` to the report folder, relaunch OBS, and continue.
- Stop and ask the user when the environment blocks most of the remaining cases (the OBS build fails, OBS does not start, the receivers cannot run, the fixture cannot be built).

## Step 3 — Clean up

Skip this step when the run stopped before every selected case finished, so that it can be resumed.

1. Quit OBS and the receivers, and stop the display.
2. Copy `<config>/logs/` to the report folder.
3. Delete `<work>` with `.claude/scripts/del-tmp.sh <work>`. When it fails, give the path to the user to delete instead. Keep `<obs>`.

## Step 4 — Report

Tell the user:

- The report path.
- The OBS version tested, and the path and size of `<obs>` (delete it with `.claude/scripts/del-tmp.sh` when no longer needed).
- The counts of PASS / FAIL / SKIP / BLOCKED.
- Each failure in one line: case ID, whether it reproduced, and the related issue or "new".
- What this machine could not cover (skipped cases and the missing prerequisites).
