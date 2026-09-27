# Regression cases (macOS)

Scope `all` runs on 32.2, 31.1, and 30.1.2; scope `latest` runs on 32.2 only. Issue numbers name the bug an item guards against. "Within 5 s" limits are pass criteria; a longer wait is a failure. The dock refreshes statuses every 2 s; judge a status after the next refresh.

## Fixture

Build it on each instance.

- Media in `<work>/media/`, generated with ffmpeg once for all versions:
  - `quad.png`: 1280x720, four solid-color quadrants in distinct colors. `quad-small.png`: the same at 640x360.
  - `quad-1k.mp4`: `quad.png` as 30 fps video with a 1 kHz sine tone, 10 s.
  - `tone440.wav`: 440 Hz sine, 10 s.
- Profiles `BORegression` and `BORegression2`: Simple output mode, x264, base and output resolution 1280x720 at 30 fps, recording path `<root>/work/obs-rec`, replay buffer enabled, streaming service Custom pointing at a local RTMP receiver.
- Scene collection `BORegression`:
  - Scene `Main`: media source `Media` (`quad-1k.mp4`, loop, filling the canvas), image source `Quad` (`quad.png`), media source `Tone440` (`tone440.wav`, loop), text source `Name` (`text_ft2_source_v2`).
  - Scene `Other`: one color source.
- Scene collection `BORegression2`: one scene with one source that has a Branch Output filter.

Defaults unless a case says otherwise: one Branch Output filter on `Media` with x264, Save Path `<root>/work/bo-rec`, dock Interlock "Always ON", every dock checkbox on, and `Main` in Program. Remove filters left by earlier cases when they would affect the result.

Press the properties' Apply once after adding a filter in any case (for example the filter on the scene `Main` in R09): a newly added filter, and a filter loaded with every output type off, starts no output until then, even when obs-websocket sets its settings and enables it. Settings changed through obs-websocket after that take effect.

The hardware encoder in R01 and R08 is Apple VT H264 Hardware Encoder.

On 30.1.2, never stop a recording that goes through ffmpeg-mux while another one that started later is still running, OBS's own recording included (every recording format of 30.1.2 uses ffmpeg-mux; a replay buffer spawns a muxer only for the duration of a save). 30.1.2 sets no `FD_CLOEXEC` on the muxer pipes, so each later `obs-ffmpeg-mux` process inherits the input pipes of the earlier ones, and stopping the earlier recording waits until the later muxers exit; the plugin's synchronous stop and its interlock turn that wait into a permanent deadlock.

- R11: turn the filter's recording off in the dock before stopping OBS's recording. SKIP the check that the filter's recording stops with OBS's recording, judge R11 on its remaining items, and list the skipped check in the Notes column and under "Not covered".
- R12: "Deactivate All" stops every filter at once, so record on one filter only: run a recording on one filter, streaming on another, and a replay buffer on the third.
- When a stop hangs anyway, `lsof -p` on the `obs-ffmpeg-mux` processes shows the inherited PIPE fds; killing the later-started muxers releases the hang.

## R01 Startup and new filter — all

Do: delete `recently.json` from `<config>/plugin_config/osi-branch-output/` if it exists, set the profile's streaming encoder to the hardware encoder, add a Branch Output filter to `Media`, turn Stream Recording on, and Apply. Afterwards set the profile encoder back to x264, switch the filter to x264 in its properties, and Apply (a new filter copies the last applied filter's settings from `recently.json`). Then record Matroska with it (Hybrid MP4 bypasses ffmpeg-mux, where #208 failed).

Pass:

- The Docks menu has "Branch Output Status", and it lists the new filter's rows.
- The new filter's Video Encoder is the profile's hardware encoder, and the recording it writes plays. With no hardware encoder on the machine, use x264, leave out the switch to x264 with its pass item, and note both.
- After the switch to x264 (#208): the properties show the x264 preset `veryfast`, the log has the x264 encoder's `preset: veryfast` and no `Invalid preset` line, and the Matroska recording plays.

## R02 Streaming — all

Do: turn Streaming on with Stream Count 2, each slot pointing at its own local RTMP receiver, and Apply.

Pass:

- Both receivers get video and the 1 kHz tone; the dock shows both slots "Live" with growing Sent Size and Bitrate.
- Unchecking slot 2 in the dock stops only slot 2; checking it again resumes it.
- Stopping receiver 1 turns slot 1 to "Reconnecting"; restarting the receiver brings it back to "Live" with data arriving.

Edge:

- Disabling the filter (eye icon) while slot 1 is "Reconnecting": OBS stays responsive and every row turns "Inactive" within 5 s.
- Slot 1 set to an SRT listener URL (`srt://127.0.0.1:{port}?mode=listener`) with no caller: enabling and then disabling the filter turns the rows "Inactive" within 5 s, and OBS does not freeze. Enabling it again and connecting an ffmpeg SRT caller delivers data without a crash.

## R03 Recording — all

Do: Streaming off, Stream Recording on, a file name format containing `%1` and `%2`. Record Matroska, then Hybrid MP4 (skip Hybrid MP4 on 30.1.2, which lacks it). Then turn "Use profile's recording path" on and record once more.

Pass:

- Each file's name expands `%1` to the source name and `%2` to the filter name.
- ffprobe: 1280x720 video, one audio stream carrying the 1 kHz tone, duration within 1 s of the time between the recording's start and stop log lines (1.5 s on 30.1.2, whose own recordings fall short by as much).
- With "Use profile's recording path", the file is written to the profile's recording path instead of the Save Path.

## R04 Recording control — all

Do: record Matroska with "Generate File Name without Space" on and a file name format containing spaces. First use "Split by Size" with a small size (for example 2 MB). Then use "Only split manually" and operate the dock: the row's Split / Pause / Unpause buttons and the Split All / Pause All / Unpause All buttons. On Hybrid MP4 (not 30.1.2), also add a chapter from the dock.

Pass:

- Split by size produces several consecutive playable files, and no file name contains a space.
- Each manual split starts a new file.
- Pause shows "Paused" and Unpause returns to "Recording"; the file's duration excludes the paused time.
- Hybrid MP4: the chapter appears in `ffprobe -show_chapters`.

Edge: split within the first second after the recording starts. No empty file results, and the first file's audio decodes (#191).

## R05 Replay buffer — all

Do: on two filters, turn Replay Buffer on with Maximum Replay Time 10 s and "Show estimated memory usage" on, and set the video encoder's keyframe interval to 1 s. Wait more than 10 s, save with one row's Save button, then with "Save All Replay Buffers".

Pass:

- The dock shows "Buffering", and the properties show an estimated memory usage in MB.
- Each save writes one file per saved filter, about 10 s long and at most 11 s (a save keeps whole keyframe intervals, so it can exceed the maximum by one interval), with video and audio, and logs `Replay buffer saved`.
- The saved file names expand `%1` and `%2` as in R03.

## R06 Audio sources — latest

Do: record about 10 s with each audio setting: Custom Audio Source off (filter audio); custom source `Tone440`; Master Audio track 1; No Audio; Multitrack Audio with track 1 = `Tone440`, track 2 = filter audio, and one track Disabled.

Pass (check the audio stream count and each stream's dominant frequency):

- Filter audio: 1 kHz only. `Tone440`: 440 Hz only. Master Audio: both tones. No Audio: one silent audio stream.
- Multitrack: one audio stream per enabled track with the expected tone; the disabled track has no stream.

## R07 Video output — all

Do: record about 5 s with each setting: Resolution "Half of source (50%)"; Custom 640x360 with Frame Rate Divider 1/2; Relative crop keeping only the top-left quadrant; Absolute crop selecting the bottom-right quadrant. With the properties open, turn "Preview Cropping Rect" on, then close the properties.

Pass:

- Output sizes: 640x360 each. The divider case runs at half the canvas frame rate (15 fps).
- Each crop output is filled with the color of the selected quadrant.
- The crop rectangle is drawn over the source while the preview option is on and the properties are open, and disappears when they close.

## R08 Filter input mode — all

Do: set Video Source to "Filter Input (Experimental)" on the filter on `Media`. Add a Color Correction filter that visibly changes the colors, first above Branch Output, then below it. Record each order with x264 and with the hardware encoder. Then add a Branch Output filter in Filter Input mode directly to `Quad` with no other filter on it, and record.

Pass:

- The effect is in the output when the Color Correction filter is above Branch Output, and absent when it is below.
- Both encoders produce correct 1280x720 content. With no hardware encoder on the machine, note it.
- `Quad`: the output shows the image, not black, and the OBS preview still shows `Quad` (#170).
- No "Branch Output Proxy" source appears in the Sources list or in any source picker.

## R09 Blanking and mute — all

Do: turn on "Blank output when source is not in Main Output" and "Mute audio while blanked", with Multitrack Audio track 1 = Master Audio and track 2 = filter audio. Record while switching Program from `Main` to `Other` and back. Then, with `Other` in Program, disable and re-enable the filter.

Pass:

- While `Other` is in Program: frames are black, every audio track is silent, and the dock status ends with "(Blank+Mute)". Back on `Main`: the picture and the tones return.
- Enabling the filter while `Other` is in Program blanks from the first frame.
- Studio Mode, with `Main` only in Preview and `Other` in Program: the output is blanked. Transitioning `Main` to Program ends the blanking.
- Studio Mode, with a blanking Branch Output filter on the scene `Main` itself: not blanked while `Main` is in Program, blanked while `Main` is only in Preview. Judge this filter by video: its filter audio is always silent, because a scene passes no audio filter.

The `BranchOutputFilter destroyed` line each Studio Mode transition logs (the filter on Studio Mode's discarded scene copy) is not a failure.

## R10 Interlock modes — latest

Do: with the filter's streaming slot pointing at a local RTMP receiver (an SRT listener adds up to 5 s to each stop) and recording on, set each dock Interlock value in turn (Always ON, Streaming, Recording, Streaming or Recording, Replay Buffer, Virtual Cam as described below, Always OFF), and start and stop the matching OBS outputs.

Pass:

- The filter's outputs run exactly while the interlock condition holds (Always ON: always; Always OFF: never), starting and stopping within 5 s.
- An unchecked dock checkbox keeps its output off even while the condition holds.

Virtual Cam: the virtual camera is a system extension that runs only from `/Applications/OBS.app`. Test the Virtual Cam value on 32.2 when both conditions below hold; otherwise SKIP it, give the unmet condition as the reason, and do not start the virtual camera.

- `/Applications/OBS.app` is the same release as the 32.2 under test.
- `systemextensionsctl list` shows `com.obsproject.obs-studio.mac-camera-extension` of that release as `[activated enabled]`.

For this value only, launch `CFFIXED_USER_HOME=<root>/home /Applications/OBS.app/Contents/MacOS/OBS --disable-updater` with the 32.2 instance's `<root>`, run the launch checks of the ground rules, and confirm with `lsof` that `osi-branch-output` loads from `<config>/plugins/`. Never change files inside `/Applications/OBS.app`.

When the virtual camera still fails to start, SKIP the value with the failure as the reason: an extension whose version just changed works only after the machine reboots.

## R11 Individual interlock — all

Do: set Interlock to "Individual", with streaming, recording, and replay buffer on in the filter. Start and stop OBS streaming, recording, and replay buffer one at a time.

Pass:

- Each Branch Output type runs exactly while its OBS counterpart runs (streaming, recording, replay buffer), independently of the others.
- The dock checkboxes turn single outputs off and back on while their counterpart runs.

Edge: with only OBS recording running, change a filter setting and Apply. Only the recording restarts: no Branch Output stream goes live, no extra recording file of about 1 s appears, and no replay buffer starts (#189).

## R12 Status dock — all

Do: run outputs on three filters (on `Media`, on `Quad`, and on the scene `Main`).

Pass:

- "Deactivate All" and "Activate All" disable and enable every filter, and the filters' eye icons follow.
- A row's Reset sets only that row's dropped frames back to 0, also after one filter is deleted and another added. "Reset All" does so on every row (#182). Sent Size returns to the cumulative total at the next refresh (#188).
- Clicking a recording or replay buffer row's folder cell opens its save folder in Finder.
- The Interlock value is kept per profile: set different values in `BORegression` and `BORegression2`; switching profiles shows each profile's own value.

## R13 Hotkey actions — all

Do: in Settings → Hotkeys, assign unused combinations (for example Control+Option+Shift+{key}) to one hotkey of each kind: the filter's Enable / Disable, Enable / Disable All Branch Outputs, the filter's all-streaming and slot-1 Enable / Disable, recording Enable / Disable, replay buffer Enable / Disable, Split, Pause, Unpause, Add chapter, Save replay buffer, and the "all" variants of split, pause, unpause, chapter, and save. Press each with OBS frontmost and its main window focused, with the filter's streaming off while testing Pause and Unpause (pausing is refused while the filter streams). After a Split key, wait for the new file before pressing a Pause key: a split still pending at Pause waits until after Unpause. Keep the assignments for R14.

Pass:

- Each key acts on its target only, and the dock checkboxes update immediately.
- The "all" hotkeys also work with the dock closed.
- Add chapter acts only on Hybrid MP4 recordings; on 30.1.2 it is expected to do nothing.

## R14 Hotkey persistence — all

Do and Pass: after each operation below, Settings → Hotkeys shows the R13 assignments.

- Turn Stream Recording off, Apply, turn it on, Apply: the recording group has all 6 items (Split, Pause, Unpause, Add chapter, Enable, Disable) with their keys. Do the same for Replay Buffer: 3 items (Save, Enable, Disable).
- Rename the filter: keys kept, and the descriptions show the new name.
- Delete the filter, then Undo: keys restored, and the outputs run again.
- Restart OBS: keys kept.
- Launch with `--safe-mode`, exit, and launch normally: keys kept.
- Switch to `BORegression2` and back: keys kept, including the "all" hotkeys.

## R15 Apply semantics — all

Do and Pass:

- Change the resolution in the properties without pressing Apply: the running outputs continue and the dock does not change. Press Apply: the outputs restart once with the new resolution (a single `Settings change detected, Attempting restart` line).
- While streaming runs, turn the Replay Buffer group on without Apply: no replay buffer starts. Apply: the outputs restart once, the replay buffer shows "Buffering" afterwards, and OBS stays responsive (#184).

## R16 Source availability — latest

Do and Pass, with a filter on `Quad` and recording on:

- Point `Quad` at `quad-small.png`: the output restarts at 640x360. With "Don't reset output when source resolution changes" on, it does not restart.
- With "Suspend recording when source is not available" on and recording only: renaming `quad.png` away shows "Paused"; restoring it returns to "Recording". With streaming also on: renaming away shows "Pending" for the recording, and restoring it restarts the recording.
- While the recording is "Pending", turn off every output type and Apply: the rows become "Inactive", and `Settings change detected` does not repeat every second (#194).
- Delete `Quad` from the scene: its outputs stop. Undo: the source returns and its outputs run again.

## R17 File name override scripts, Lua — all

Do: load `recording-filename-from-text.lua` and `replay-buffer-filename-from-text.lua` from `<config>/plugins/osi-branch-output.plugin/Contents/Resources/scripts/`: with OBS closed, append `{"path": "{absolute path}", "settings": {}}` for each to `modules.scripts-tool` in `<config>/basic/scenes/BORegression.json`. In Tools → Scripts, set each script's text source to `Name` and its filter to the one on `Media`. Record with "Only split manually".

Pass:

- Each script's filter list shows every Branch Output filter of the collection.
- The recording file name follows the text. Changing the text during the recording splits to a new file with the new name (the script applies one change per 30 s per distinct text).
- The next replay buffer save uses the text-based name.
- A text change while the recording is paused takes effect only after Unpause.
- Removing the scripts restores the filter's own file name format, splitting the running recording to a new file in that format.

Edge: with the recording script loaded and the filter recording with splitting enabled, restart OBS. The first recording file after startup is not empty and its audio decodes (#191).

## R18 File name override scripts, Python — latest

Do and Pass: R17's Do, Pass, and Edge with the `.py` scripts.

- Use a framework build of Python 3.6–3.12 whose architectures include the machine's (the python.org installer's universal2 build qualifies): OBS loads `{path}/Python.framework/Versions/3.X/lib/libpython3.X.dylib`, trying 3.12 down to 3.6, and logs `Could not load library` when none exists.
- With OBS closed, set `Path64bit` under `[Python]` in `<config>/user.ini` to the folder that contains `Python.framework` (for example `/Library/Frameworks`), not the version folder. After launch, confirm the loaded version in Tools → Scripts → Python Settings.
- SKIP when no such Python is installed.

## R19 Lifecycle and shutdown — all

Do and Pass:

- Toggle the filter's eye icon five times quickly while every output runs: the filter ends in the last state, and no failure line is logged.
- Switch to the scene collection `BORegression2` and back, once with outputs running and once with them stopped: no crash, and the dock lists only the current collection's filters.
- Quit OBS once with outputs running and once with them stopped, each from a session launched for this item: shutdown completes, no new `OBS*.ips` appears in `~/Library/Logs/DiagnosticReports/`, and the log ends with `Number of memory leaks: 0`. Other sessions can end with leaks of OBS itself (the first session of an instance; on 30.1.2, one per SRT `Failed to open the url`).

## Main output

A main output is a Branch Output whose input is OBS's program output instead of a source. It belongs to a profile and is saved in `branchOutputPrograms.json` in the profile's folder, `<config>/basic/profiles/{profile}/`.

- Add one with the "Add Main Output" button at the left end of the dock's bottom button row. It creates `Main Output {N}`, with the smallest number that no main output of the profile uses, and opens its properties in OBS's standard properties dialog. Its dock rows show the name in the Filter column and `Main Output` in the Source column; clicking the Source cell opens the dialog.
- Change its settings through obs-websocket `SetInputSettings` with `inputUuid` set to its `uuid` in `outputs` of `branchOutputPrograms.json`, read from the file each time. The change applies at once, like the dialog's OK, with no Apply. A main output is a private source: obs-websocket finds it by UUID, not by name. Type no text or numbers into the dialog, for the reason in SKILL.md's "Avoid typing".
- Use the dialog only in the steps a case marks "in the dialog", where the case checks the dialog itself. There too, operate only lists, checkboxes, and buttons.
- Change no setting through obs-websocket while a main output's dialog is open: the dialog's Cancel restores and reapplies the settings it opened with.
- Keep the default name.
- Close with Cancel a dialog opened only to look.
- Before M01, reduce the program picture to the four quadrants of `quad.png`, because state left by the R cases skews the color checks:
  - Delete every filter the R cases left on the sources and scenes of `BORegression`, of any kind (Branch Output, Color Correction, and so on).
  - Point `Quad` back at `quad.png`.
  - Hide `Name` (obs-websocket `SetSceneItemEnabled`).
- Defaults unless a case says otherwise: no filter in `BORegression` other than those the case adds; one main output, `Main Output 1` from M01, with x264, Stream Recording on, and Resolution at its default "Output (Stretch to fit)"; dock Interlock "Always ON", every dock checkbox on, `Main` in Program, and Studio Mode off.
- Keep a main output's recordings and replay buffer saves in its default folder, the profile's recording path, where OBS's own recordings also go, and tell the files apart by name. The default file name format is `%1 %2 {the profile's format}`, where `%1` expands to `Main Output` and `%2` to the main output's name.
- M02–M10 use M01's main output. When it is missing (for example on a resume in a rebuilt work folder), first create it as M01's Do does, with x264.
- To remove a main output (to redo a case, or to resume R cases on the same version), delete its element from `outputs` in `branchOutputPrograms.json` while OBS is closed. To delete the whole file, also delete `branchOutputPrograms.json.bak` and any `branchOutputPrograms.json.tmp`: the plugin loads the `.bak` when it cannot read the file. The R cases assume that the profile has no main output.

While #208 is open (checked as above), set `codec_type` to 0 through obs-websocket, by `inputUuid`, on each main output after its first dialog closes and before it records with an encoder other than Apple VT, and record the workaround under "Plugin, known issues". The items under the #208 paragraph above apply to main outputs as well.

- M01: set `codec_type` to 0 in the same `SetInputSettings` call that switches to x264: a separate call restarts the output for `codec_type` alone and splits the Apple VT recording into two files.
- M08: set it on both main outputs added there.

The 30.1.2 ffmpeg-mux rule above applies to main output recordings too. In the M cases, keep the main output's recording the only one running: before M10, turn off the recording of the filter in the scene collection `BORegression2` if it is on.

### M01 Add and configure — all

Do: delete `recently.json` as in R01, and set the profile's streaming encoder to the hardware encoder of R01. Press "Add Main Output", check the items of the dialog it opens (turn Custom Audio Source on to see the audio source list, and off again), turn Stream Recording on in the dialog, and close it with OK without pressing the dialog's Apply. Record about 10 s. Then set the profile encoder back to x264, set the main output's `video_encoder` to x264, and record again.

Pass:

- The dock lists `Main Output 1`'s rows, with `Main Output 1` in the Filter column and `Main Output` in the Source column.
- The recording starts within 5 s of the OK, and the dock's recording row shows "Recording" (unlike a filter, without the dialog's Apply).
- The dialog has no "Video Source" list, no "Blank output when source is not in Main Output" or "Mute audio while blanked", and no "Filter Audio" among the audio sources. Resolution defaults to "Output (Stretch to fit)".
- The new main output's Video Encoder is the profile's hardware encoder. Without a hardware encoder, use x264 and note it, as in R01.
- Both recordings play, are 1280x720, and show the four quadrants of `quad.png` in the program's layout. Each has one audio stream carrying both the 1 kHz and the 440 Hz tone (the default audio is master track 1).
- The file names expand `%1` to `Main Output` and `%2` to `Main Output 1`.
- `branchOutputPrograms.json` in the profile's folder lists `Main Output 1`.
- The source add menu has no "Branch Output Main Output" or "Branch Output Program Proxy", and no source derived from the main output appears in the Sources list or in any source picker (as R08 checks for "Branch Output Proxy").

### M02 Apply semantics — all

Do and Pass, every step in the dialog, with `Main Output 1` recording. Count restarts by the `Settings change detected, Attempting restart` lines.

- Open the dialog and press Cancel without a change: it closes without OBS's unsaved-changes prompt ("There are unsaved changes. Do you want to keep them?"), and no restart.
- Set Resolution to "Half of canvas (50%)" and press Cancel: no restart, and on reopening Resolution is "Output (Stretch to fit)".
- Set Resolution to "Half of canvas (50%)" and press the dialog's Apply: one restart with the dialog still open, and the recording is 640x360. Then press OK: no restart.
- Open the dialog and press OK without a change: no restart (unlike a filter's Apply without a change, #178).
- Open the dialog, set Resolution back to "Output (Stretch to fit)", press the dialog's Apply (one restart), and then Cancel: one more restart, back to 640x360, the settings the dialog opened with. On reopening Resolution is "Half of canvas (50%)". Finally set "Output (Stretch to fit)" and press OK: one restart, back to 1280x720.
- Turn Replay Buffer on without applying: no replay buffer starts. Press OK: one restart, then "Buffering". Afterwards turn Replay Buffer off through obs-websocket.

### M03 Streaming, recording, and replay buffer — all

Do: turn Streaming on with Stream Count 2 (each slot pointing at its own local RTMP receiver), Stream Recording on, and Replay Buffer on with Maximum Replay Time 10 s and the video encoder's keyframe interval 1 s. Wait more than 10 s, then save with the replay buffer row's Save button.

Pass:

- Both receivers get 1280x720 video and both tones. The dock shows both slots "Live" with growing Sent Size and Bitrate, the recording "Recording", and the replay buffer "Buffering".
- Unchecking slot 2 in the dock stops only slot 2; checking it again resumes it.
- The recording plays, and its duration meets R03's criterion.
- The save meets R05's criteria: one file, about 10 s long and at most 11 s, with video and audio, and `Replay buffer saved` logged.

Afterwards turn Streaming and Replay Buffer off, keeping Stream Recording on.

### M04 Program output, transitions, and Studio Mode — all

Do: `Main Output 1` records only. Through obs-websocket, set the transition to Fade with a duration of 2000 ms. While recording, switch Program from `Main` to `Other` and back to `Main`. Then turn Studio Mode on with `Main` in Program and `Other` in Preview, wait 3 s, open the dialog from the Source cell, check its preview, close it with Cancel, and transition `Other` to Program. Finally turn Studio Mode off, set the transition duration back to 300 ms, and put `Main` back in Program.

Pass:

- While `Main` is in Program the output shows the four quadrants, and while `Other` is in Program the solid color of `Other`. Frames during each 2 s fade blend the two (neither pure color).
- Studio Mode: while `Other` is only in Preview the output shows `Main`; after the transition it shows `Other`.
- The dialog's preview opened in Studio Mode shows the Program (`Main`).
- No dock status ends with "(Blank)" or "(Blank+Mute)".

### M05 Video output — all

Do:

1. Record about 5 s with each setting: Resolution "Half of canvas (50%)"; Custom 640x360 with Frame Rate Divider 1/2.
2. Set Resolution back to "Output (Stretch to fit)" with no divider, set a Relative crop keeping only the top-left quadrant, and also set the Absolute crop values to the bottom-right quadrant, keeping `crop_type` Relative. Record about 5 s.
3. With the recording running, open the dialog from the Source cell, turn "Preview Cropping Rect" on in the dialog, and check the preview. Switch Cropping to "Absolute (Region)" in the dialog, check the preview, wait about 5 s, close the dialog with OK, and record about 5 s.
4. Reopen the dialog, turn only "Preview Cropping Rect" on in it, and close it with OK.
5. Set `crop_type` back to no crop. Set the dock Interlock to "Always OFF"; once every row is "Inactive", set Settings → Video → Output (Scaled) Resolution to 640x360 and press OK. Set Interlock back to "Always ON" and record about 5 s. Finally restore 1280x720 the same way (Always OFF, every row "Inactive", 1280x720, Always ON).

Pass:

- Step 1: each output is 640x360. The divider recording runs at half the canvas frame rate (15 fps).
- Step 2: the output is 1280x720 (OBS's output resolution, the cropped quadrant stretched to fit) and filled with the top-left quadrant's color. After the OK of step 3, it is filled with the bottom-right quadrant's color.
- Step 3: the dialog's preview shows the whole uncropped program with a rectangle drawn over it, around the top-left quadrant once "Preview Cropping Rect" is on and around the bottom-right quadrant once "Absolute (Region)" is selected. Until the OK, the output stays on the top-left quadrant and does not restart. OBS's main preview shows no rectangle.
- Step 4: on reopening, "Preview Cropping Rect" is off and the preview shows no rectangle. The OK after turning only it on causes no restart.
- Step 5: the recording is 640x360, and changing the video settings does not crash OBS.

### M06 Audio sources — latest

Do: record about 10 s with each audio setting, Custom Audio Source on: source `Tone440`; Master Audio track 1; No Audio; Multitrack Audio with track 1 = Master Audio track 1, track 2 = `Tone440`, and track 3 = Disabled. Keep recording with the Multitrack setting, switch the scene collection to `BORegression2`, wait about 10 s, and switch back. Finally turn Custom Audio Source off.

Pass (check the audio stream count and each stream's dominant frequency, as in R06):

- `Tone440`: 440 Hz only. Master Audio: both tones. No Audio: one silent audio stream.
- Multitrack: one audio stream per enabled track (both tones; 440 Hz); the Disabled track has no stream.
- The recording file started while `BORegression2` is loaded has one audio stream (Master Audio), and the log shows `Ignore audio source for track 2`. The file started after returning to `BORegression` has two streams again, the second at 440 Hz.

### M07 Status dock and interlock — all

Do: add a Branch Output filter to `Media` with only Streaming on, pointing at its own local RTMP receiver, and Apply. On `Main Output 1`, turn Streaming on with one slot pointing at another receiver, Stream Recording on, Replay Buffer on, and Automatic File Splitting set to "Only split manually" (without a split setting, Split All does nothing).

Pass:

- Disabling `Main Output 1` with the eye icon in its Filter cell turns its rows "Inactive" within 5 s while the filter keeps running. Enabling it resumes its outputs.
- "Deactivate All" and "Activate All" stop and resume both the filter and `Main Output 1`, and their eye icons follow.
- "Split All" splits `Main Output 1`'s recording to a new file, and "Save All Replay Buffers" saves `Main Output 1`'s replay buffer.
- A `Main Output 1` row's Reset sets only that row's dropped frames back to 0.
- Clicking the folder cell of `Main Output 1`'s recording or replay buffer row opens its save folder in Finder, judged as in R12.
- Clicking a `Main Output 1` Source cell opens its dialog, OBS's standard properties dialog. Close it with Cancel.
- With Interlock set to Streaming, start and stop OBS's streaming: `Main Output 1`'s outputs run only while OBS streams, starting and stopping within 5 s, and OBS's own stream reaches the profile's receiver with video and audio.

Afterwards set Interlock back to "Always ON", delete the filter on `Media`, and turn `Main Output 1`'s Streaming, Replay Buffer, and Automatic File Splitting off.

### M08 Persistence — all

Do and Pass, with Stream Recording and Replay Buffer on in `Main Output 1`:

- Uncheck the replay buffer row in the dock, then restart OBS: `Main Output 1` is listed with its settings kept (Stream Recording and Replay Buffer on, x264), its recording runs again, and its replay buffer stays unchecked, without "Buffering". Check it again.
- Disable `Main Output 1` with the eye icon in its Filter cell, then restart OBS: it is listed disabled and runs nothing. Enabling it starts its outputs.
- While it records, switch the profile to `BORegression2`: `Main Output 1`'s rows leave the dock, its recording stops, and the file plays. In `BORegression2`, set the dock Interlock to "Always ON" and press "Add Main Output" twice, closing each dialog with Cancel without a change: each closes without OBS's unsaved-changes prompt (M02), and `Main Output 1` and `Main Output 2` appear. Turn only Replay Buffer on in `BORegression2`'s `Main Output 1`.
  - Switch back to `BORegression`: only `BORegression`'s `Main Output 1` is listed, with its settings kept, and its recording runs again.
  - Switch to `BORegression2` again: its two main outputs are listed, `Main Output 1` shows "Buffering", and `Main Output 2` runs nothing.
  - Each profile's `branchOutputPrograms.json` lists only that profile's main outputs (two in `BORegression2`).
  - Finally switch back to `BORegression`.

Afterwards turn Replay Buffer off.

### M09 Script filter list — all

Do: add a Branch Output filter to `Media` and Apply. Load `recording-filename-from-text.lua` as R17's Do does, and open that script's filter list in Tools → Scripts.

Pass: the list has the filter on `Media` and no `Main Output 1`.

Afterwards remove the script and delete the filter on `Media`.

### M10 Lifecycle and shutdown — all

Do and Pass, where "outputs running" means `Main Output 1`'s Streaming, Stream Recording, and Replay Buffer all running:

- Toggle the eye icon in `Main Output 1`'s Filter cell five times quickly while its outputs run: it ends in the last state, and no failure line is logged.
- Switch the scene collection to `BORegression2` and back, once with outputs running and once with `Main Output 1` disabled by its eye icon: no crash, and after each switch the dock lists each of `Main Output 1`'s rows once (they may disappear during the switch). With outputs running, they stop during the switch and run again after it: each switch starts a new recording file, and the receiver gets data again.
- Quit OBS from three sessions, each launched for this item: one with outputs running, one with them stopped, and one with outputs running after switching the profile to `BORegression2` and back and then the scene collection to `BORegression2` and back. Each shutdown completes with no crash evidence, and the log ends with `Number of memory leaks: 0` (crash evidence and allowed leaks as in R19).
  - When only the session with the switches ends with leaks, repeat it with no main output, after moving `branchOutputPrograms.json` and its `.bak` of both `BORegression` and `BORegression2` aside while OBS is closed. If that session also leaks, record it under "OBS" in "Observations" and do not fail the item; otherwise fail the item. Finally move the files back.
