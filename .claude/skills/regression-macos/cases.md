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

Stopping a reconnecting RTMP slot while libobs is inside a connect attempt (between `Connecting to RTMP URL` and the attempt's result) freezes OBS on macOS (#238). When OBS stops responding right after such a stop (R02 Edge, M11's removal while reconnecting, M13's Edge), take a `sample` of the process, record FAIL with #238, kill OBS, and continue.

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

- Disabling the filter (eye icon) while slot 1 is "Reconnecting": OBS stays responsive, slot 2 stops at once, and slot 1 stops within 3 s of its output's last `Reconnecting in` line before the stop: the plugin defers the stop of a reconnecting slot until 2 s after libobs' last reconnect signal, so the whole stop can take more than 5 s (#214). Every row is "Inactive" at the next dock refresh after that.
- Slot 1 set to an SRT listener URL (`srt://127.0.0.1:{port}?mode=listener`) with no caller: enabling and then disabling the filter turns the rows "Inactive" within 5 s, and OBS does not freeze. Enabling it again and connecting an ffmpeg SRT caller delivers data without a crash.

## R03 Recording — all

Do: Streaming off, Stream Recording on, a file name format containing `%1` and `%2`. Record Matroska, then Hybrid MP4 (not on 30.1.2), then Hybrid MOV (32.2 only). Then turn "Use profile's recording path" on and record once more.

Pass:

- Each file's name expands `%1` to the source name and `%2` to the filter name.
- ffprobe: 1280x720 video, one audio stream carrying the 1 kHz tone, duration within 1.5 s of the time between the recording's start and stop log lines. Note the shortfall in the Notes column: Branch Output drops the frames the encoder still holds at the stop (#232), about 1.0–1.3 s with x264 at 30 fps.
- Hybrid MOV: the file ends in `.mov`, and ffprobe shows the major brand `qt  `.
- With "Use profile's recording path", the file is written to the profile's recording path instead of the Save Path.

Edge:

- 31.1 and 30.1.2 lack Hybrid MOV's output type, and 30.1.2 also Hybrid MP4's: the properties list such a format grayed out and unselectable. Set `rec_format` to `hybrid_mov` through obs-websocket: no recording starts, the log has `Recording format 'hybrid_mov' requires output type 'mov_output', which this OBS does not provide` (repeated each second while the setting stays), and opening and closing the properties leaves `rec_format` at `hybrid_mov`. On 30.1.2, do the same with `hybrid_mp4` (`mp4_output`).
- 32.2, Hybrid MOV with the filter's `audio_encoder` set to `ffmpeg_opus`: no recording starts, and the log has `Recording format 'hybrid_mov' does not support codec 'opus'`. With `ffmpeg_pcm_s16le` it records, and the file's audio carries the 1 kHz tone. Hybrid MP4 with `ffmpeg_opus` still records. Restore the AAC encoder afterwards.

## R04 Recording control — all

Do: record Matroska with "Generate File Name without Space" on and a file name format containing spaces. First use "Split by Size" with a small size (for example 2 MB). Then use "Only split manually" and operate the dock: the row's Split / Pause / Unpause buttons and the Split All / Pause All / Unpause All buttons. On Hybrid MP4 (not 30.1.2) and Hybrid MOV (32.2 only), also split once and add a chapter from the dock.

Pass:

- Split by size produces several consecutive playable files, and no file name contains a space.
- Each manual split starts a new file.
- Pause shows "Paused" and Unpause returns to "Recording"; the file's duration excludes the paused time.
- Hybrid MP4 and Hybrid MOV: the file after the split keeps the format's extension (`.mp4` / `.mov`), and the chapter appears in `ffprobe -show_chapters`.

Edge: split within the first second after the recording starts. No empty file results, and the first file's audio decodes (#191).

## R05 Replay buffer — all

Do: on two filters, turn Replay Buffer on with Maximum Replay Time 10 s and "Show estimated memory usage" on, and set the video encoder's keyframe interval to 1 s. Wait more than 10 s, save with one row's Save button, then with "Save All Replay Buffers".

Pass:

- The dock shows "Buffering", and the properties show an estimated memory usage in MB.
- Each save writes one file per saved filter, about 10 s long and at most 11 s (a save keeps whole keyframe intervals, so it can exceed the maximum by one interval), with video and audio, and logs `Replay buffer saved`.
- The saved file names expand `%1` and `%2` as in R03.

Edge, 32.2 only: with OBS closed, set `RecFormat2=hybrid_mov` under `[SimpleOutput]` in the profile's `basic.ini`, and delete `recently.json` (R01). Add a new filter, turn Replay Buffer on, and Apply: the properties show Hybrid MOV as the recording format and MOV as the replay buffer format, and a save writes a `.mov` file with video and audio. Restore `RecFormat2` the same way afterwards.

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

Do: turn on "Blank output when source is not in Program" and "Mute audio while blanked", with Multitrack Audio track 1 = Master Audio and track 2 = filter audio. Record while switching Program from `Main` to `Other` and back. Then, with `Other` in Program, disable and re-enable the filter.

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
- A row's Reset sets only that row's dropped frames back to 0, also after one filter is deleted and another added. "Reset All" does so on every row (#182). Both also set Sent Size back to 0, and later refreshes count it up from there (#188); replay buffer rows leave it empty.
- Clicking a recording or replay buffer row's folder cell opens its save folder in Finder.
- The Interlock value is kept per profile: set different values in `BORegression` and `BORegression2`; switching profiles shows each profile's own value.

## R13 Hotkey actions — all

Do: in Settings → Hotkeys, assign unused combinations (for example Control+Option+Shift+{key}) to one hotkey of each kind: the filter's Enable / Disable, Enable / Disable All Branch Outputs, the filter's all-streaming and slot-1 Enable / Disable, recording Enable / Disable, replay buffer Enable / Disable, Split, Pause, Unpause, Add chapter, Save replay buffer, and the "all" variants of split, pause, unpause, chapter, and save. Press each with OBS frontmost and its main window focused, with the filter's streaming off while testing Pause and Unpause (pausing is refused while the filter streams). After a Split key, wait for the new file before pressing a Pause key: a split still pending at Pause waits until after Unpause. Keep the assignments for R14.

Pass:

- Each key acts on its target only, and the dock checkboxes update immediately.
- The "all" hotkeys also work with the dock closed.
- While the replay buffer runs, including after its Enable key restarts it, Settings → Hotkeys has no heading with the filter's name holding a "Save Replay" entry (#218). OBS's own "Save Replay" for the profile's replay buffer is a different entry.
- Add chapter acts only on Hybrid MP4 and Hybrid MOV recordings (on 32.2, press it on one recording of each); on 30.1.2 it is expected to do nothing.

## R14 Hotkey persistence — all

Do and Pass: after each operation below, Settings → Hotkeys shows the R13 assignments and no filter-name heading with a "Save Replay" entry (R13).

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
- Quit OBS once with outputs running and once with them stopped, each from a session launched for this item: shutdown completes, no new `OBS*.ips` appears in `~/Library/Logs/DiagnosticReports/`, and the log ends with `Number of memory leaks: 0`. In any session, OBS itself leaks one per `RTMP_ReadPacket error` line in the log: it does not free a packet when it detects an RTMP disconnect while reading, as after a case stops a receiver. Other sessions can also end with leaks of OBS itself (the first session of an instance; on 30.1.2, one per SRT `Failed to open the url`).

## Program Output

A Program Output is a Branch Output whose input is OBS's program output instead of a source. It belongs to a profile and is saved in `branchOutputPrograms.json` in the profile's folder, `<config>/basic/profiles/{profile}/`.

- Add one with the "+" button at the left end of the dock's bottom button row, choosing "Program" in its menu. It creates `Program {N}`, with the smallest number that no Program Output of the profile uses, and opens its properties in OBS's standard properties dialog. Its dock rows show the name in the Filter column and `Program` in the Source column; clicking the Source cell opens the dialog.
- Change its settings through obs-websocket `SetInputSettings` with `inputUuid` set to its `uuid` in `outputs` of `branchOutputPrograms.json`, read from the file each time. The change applies at once, like the dialog's OK, with no Apply. A Program Output is a private source: obs-websocket finds it by UUID, not by name. Type no text or numbers into the dialog, for the reason in SKILL.md's "Avoid typing".
- Use the dialog only in the steps a case marks "in the dialog", where the case checks the dialog itself. There too, operate only lists, checkboxes, and buttons.
- Change no setting through obs-websocket while a Program Output's dialog is open: the dialog's Cancel restores and reapplies the settings it opened with.
- Keep the default name, except for the renames in M11 and M12.
- Close with Cancel a dialog opened only to look.
- Before M01, reduce the program picture to the four quadrants of `quad.png`, because state left by the R cases skews the color checks:
  - Delete every filter the R cases left on the sources and scenes of `BORegression`, of any kind (Branch Output, Color Correction, and so on).
  - Point `Quad` back at `quad.png`.
  - Hide `Name` (obs-websocket `SetSceneItemEnabled`).
- Defaults unless a case says otherwise: no filter in `BORegression` other than those the case adds; one Program Output, `Program 1` from M01, with x264, Stream Recording on, and Resolution at its default "Output (Stretch to fit)"; dock Interlock "Always ON", every dock checkbox on, `Main` in Program, and Studio Mode off.
- Keep a Program Output's recordings and replay buffer saves in its default folder, the profile's recording path, where OBS's own recordings also go, and tell the files apart by name. The default file name format is `%1 %2 {the profile's format}`, where `%1` expands to `Program` and `%2` to the Program Output's name.
- M02–M14 use M01's Program Output. When it is missing (for example on a resume in a rebuilt work folder), first create it as M01's Do does, with x264.
- To remove a Program Output (to redo a case, or to resume R cases on the same version), choose "Remove" in the menu that right-clicking one of its dock rows opens (with the full-screen tools, as in M11), and answer Yes. Its rows leave the dock at once, and `Removed Program Output '{name}'` is logged once its outputs have stopped. While OBS is closed, delete its element from `outputs` in `branchOutputPrograms.json` instead. To delete the whole file, also delete `branchOutputPrograms.json.bak` and any `branchOutputPrograms.json.tmp`: the plugin loads the `.bak` when it cannot read the file. The R cases assume that the profile has no Program Output.

The 30.1.2 ffmpeg-mux rule above applies to Program Output recordings too. In the M cases, keep the Program Output's recording the only one running, except for OBS's recording in M13, run in the order M13 gives. Before M10, turn off the recording of the filter in the scene collection `BORegression2` if it is on.

### M01 Add and configure — all

Do: delete `recently.json` as in R01, and set the profile's streaming encoder to the hardware encoder of R01. Choose "+" → "Program" in the dock, check the items of the dialog it opens (turn Custom Audio Source on to see the audio source list, and off again), turn Stream Recording on in the dialog, and close it with OK without pressing the dialog's Apply. Record about 10 s. Then set the profile encoder back to x264, set the Program Output's `video_encoder` to x264, and record again.

Pass:

- The dock lists `Program 1`'s rows, with `Program 1` in the Filter column and `Program` in the Source column.
- The recording starts within 5 s of the OK, and the dock's recording row shows "Recording" (unlike a filter, without the dialog's Apply).
- The dialog has no "Video Source" list, no "Blank output when source is not in Program" or "Mute audio while blanked", no "Suspend recording when source is not available" in Stream Recording, and no "Filter Audio" among the audio sources. Resolution defaults to "Output (Stretch to fit)".
- The new Program Output's Video Encoder is the profile's hardware encoder. Without a hardware encoder, use x264 and note it, as in R01.
- Both recordings play, are 1280x720, and show the four quadrants of `quad.png` in the program's layout. Each has one audio stream carrying both the 1 kHz and the 440 Hz tone (the default audio is master track 1).
- The file names expand `%1` to `Program` and `%2` to `Program 1`.
- `branchOutputPrograms.json` in the profile's folder lists `Program 1`.
- OBS's source add menu (the "+" below the Sources list) has no "Branch Output Program" or "Branch Output Program Proxy", and no source derived from the Program Output appears in the Sources list or in any source picker (as R08 checks for "Branch Output Proxy").

### M02 Apply semantics — all

Do and Pass, every step in the dialog, with `Program 1` recording. Count restarts by the `Settings change detected, Attempting restart` lines.

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

Do: `Program 1` records only. Through obs-websocket, set the transition to Fade with a duration of 2000 ms. While recording, switch Program from `Main` to `Other` and back to `Main`. Then turn Studio Mode on with `Main` in Program and `Other` in Preview, wait 3 s, open the dialog from the Source cell, check its preview, close it with Cancel, and transition `Other` to Program. Finally turn Studio Mode off, set the transition duration back to 300 ms, and put `Main` back in Program.

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

Do: add a Branch Output filter to `Media` with only Streaming on, pointing at its own local RTMP receiver, and Apply. On `Program 1`, turn Streaming on with one slot pointing at another receiver, Stream Recording on, Replay Buffer on, and Automatic File Splitting set to "Only split manually" (without a split setting, Split All does nothing).

Pass:

- Disabling `Program 1` with the eye icon in its Filter cell turns its rows "Inactive" within 5 s while the filter keeps running. Enabling it resumes its outputs.
- "Deactivate All" and "Activate All" stop and resume both the filter and `Program 1`, and their eye icons follow.
- "Split All" splits `Program 1`'s recording to a new file, and "Save All Replay Buffers" saves `Program 1`'s replay buffer.
- A `Program 1` row's Reset sets only that row's dropped frames back to 0.
- Clicking the folder cell of `Program 1`'s recording or replay buffer row opens its save folder in Finder, judged as in R12.
- Clicking a `Program 1` Source cell opens its dialog, OBS's standard properties dialog. Close it with Cancel.
- With Interlock set to Streaming, start and stop OBS's streaming: `Program 1`'s outputs run only while OBS streams, starting and stopping within 5 s, and OBS's own stream reaches the profile's receiver with video and audio.

Afterwards set Interlock back to "Always ON", delete the filter on `Media`, and turn `Program 1`'s Streaming, Replay Buffer, and Automatic File Splitting off.

### M08 Persistence — all

Do and Pass, with Stream Recording and Replay Buffer on in `Program 1`:

- Uncheck the replay buffer row in the dock, then restart OBS: `Program 1` is listed with its settings kept (Stream Recording and Replay Buffer on, x264), its recording runs again, and its replay buffer stays unchecked, without "Buffering". Check it again.
- Disable `Program 1` with the eye icon in its Filter cell, then restart OBS: it is listed disabled and runs nothing. Enabling it starts its outputs.
- While it records, switch the profile to `BORegression2`: `Program 1`'s rows leave the dock, its recording stops, and the file plays. In `BORegression2`, set the dock Interlock to "Always ON" and choose "+" → "Program" twice, closing each dialog with Cancel without a change: each closes without OBS's unsaved-changes prompt (M02), and `Program 1` and `Program 2` appear. Turn only Replay Buffer on in `BORegression2`'s `Program 1`.
  - Switch back to `BORegression`: only `BORegression`'s `Program 1` is listed, with its settings kept, and its recording runs again.
  - Switch to `BORegression2` again: its two Program Outputs are listed, `Program 1` shows "Buffering", and `Program 2` runs nothing.
  - Each profile's `branchOutputPrograms.json` lists only that profile's Program Outputs (two in `BORegression2`).
  - Finally switch back to `BORegression`.

Afterwards turn Replay Buffer off.

### M09 File name override scripts, Lua — all

The bundled scripts list filters and Program Outputs in their "Branch Output" list, labeled `{source name} / {filter name}` and `[Program] {Program Output name}`. A script prepends its text source's text to its base format, so an overridden file name starts with the text, and logs `Recording filename format updated: {format}` (the replay buffer script: `Replay buffer filename format updated: {format}`) for each override it sends. An override belongs to the Program Output's instance: a profile switch replaces the instance and the scripts send the override again, while a profile rename keeps it.

Do: set `Name`'s `text` to `M09` through obs-websocket `SetInputSettings`. Uncheck `Program 1`'s recording row in the dock: under the 30.1.2 ffmpeg-mux rule above, the filter's recording and `Program 1`'s never run at the same time in this case. Add a Branch Output filter to `Media` with Stream Recording on, and Apply. Through obs-websocket, set Automatic File Splitting to "Only split manually" on that filter and on `Program 1`, and turn `Program 1`'s Replay Buffer on.

Do and Pass:

1. Script of the previous release: write `recording-filename-from-text.lua` of the release 1.0.13, the last whose scripts know only `get_filter_list`, to `<work>/scripts-1.0.13/` with `git show 1.0.13:data/scripts/recording-filename-from-text.lua` (`git fetch --tags` first when the tag is missing), and load it as R17's Do does. In Tools → Scripts, set its text source to `Name` and its filter to the one on `Media`.
   - Its filter list has the filter on `Media` and no `Program 1`.
   - The filter's recording splits to a file whose name starts with `M09`.
2. Update: quit OBS. In `BORegression.json`, point that script's `path` at the bundled `recording-filename-from-text.lua`, keeping its `settings`, and also load `replay-buffer-filename-from-text.lua` as R17's Do does. Launch OBS.
   - The recording script shows `Name` and `Media / Branch Output`, the filter chosen in step 1. Its Branch Output list has `Media / Branch Output` and `[Program] Program 1`, and nothing else besides "(Select a Branch Output)".
   - The filter's newest recording file starts with `M09`.
   - Switch the profile to `BORegression2`, wait 5 s, and switch back: no `Recording filename format updated` line is logged, and the filter's newest recording file still starts with `M09`.
   - `Program 1`'s recording row stays unchecked after the launch and after the profile switch, and it records nothing.
3. Program Output: in Tools → Scripts, choose `[Program] Program 1` in the recording script. Once the filter's recording has split to a file in its own format (on 30.1.2 the split waits for the next keyframe, several seconds), uncheck the filter's recording row, check `Program 1`'s recording row, and once it shows "Recording", set `Name`'s `text` to `M09B`. Finally choose `[Program] Program 1` and `Name` in the replay buffer script, and after "Buffering" for 5 s, save `Program 1`'s replay buffer with its row's Save button.
   - The choice in the recording script splits the filter's recording to a file in its own format: a script clears the override on the target it leaves.
   - `Program 1`'s recording starts with a file starting with `M09`, and the text change splits it to a file starting with `M09B`. The saved replay buffer file starts with `M09B`.
   - The later steps' "starting with `M09`" includes `M09B`.
4. Startup: delete the filter on `Media`, quit OBS, and launch it again.
   - The shutdown log ends with `Number of memory leaks: 0` (allowed leaks as in R19).
   - After the launch, `Recording filename format updated` is logged, and from then on `Program 1` records to a file starting with `M09`. A file in the default format that ends at that line is allowed: the Program Output can start recording before the script finds it.
   - No "Script Log" window opens. Each script logs `Program Output (uuid: {uuid}) not found; waiting for it to load` at most once.
5. Profile switch: switch the profile to `BORegression2`, wait 5 s, and switch back.
   - No "Script Log" window opens.
   - Back in `BORegression`, `Recording filename format updated` and `Replay buffer filename format updated` are logged again, and `Program 1` records to a file starting with `M09` (a default-format file before it, as in step 4). After "Buffering" for 5 s, a save of its replay buffer starts with `M09`.
6. Profile rename: with `Program 1` recording to a file starting with `M09`, rename the profile to `BORegressionRenamed` with Profile → Rename, wait 5 s, and rename it back to `BORegression`. Type each name directly, as M11 types `Renamed Output`, and check that the field shows exactly that text before Return.
   - `Program 1` keeps recording to the same file: no new file, and no `Recording filename format updated` or `Settings change detected` line after the renames.
   - On 31.1 and later, the log has `Keep 1 Program Output(s) across the profile change` after each rename. 30.1.2 renames a profile without a profile change event and logs none.
7. Removal: remove both scripts in Tools → Scripts.
   - `Program 1`'s recording splits to a file in its own format, and `Recording filename format override cleared` is logged.
   - Quit OBS: the log ends with `Number of memory leaks: 0` (allowed leaks as in R19).

Afterwards launch OBS, and turn `Program 1`'s Replay Buffer and Automatic File Splitting off through obs-websocket.

### M10 Lifecycle and shutdown — all

Do and Pass, where "outputs running" means `Program 1`'s Streaming, Stream Recording, and Replay Buffer all running:

- Toggle the eye icon in `Program 1`'s Filter cell five times quickly while its outputs run: it ends in the last state, and no failure line is logged.
- Switch the scene collection to `BORegression2` and back, once with outputs running and once with `Program 1` disabled by its eye icon: no crash, and after each switch the dock lists each of `Program 1`'s rows once (they may disappear during the switch). With outputs running, they stop during the switch and run again after it: each switch starts a new recording file, and the receiver gets data again.
- Quit OBS from three sessions, each launched for this item: one with outputs running, one with them stopped, and one with outputs running after switching the profile to `BORegression2` and back and then the scene collection to `BORegression2` and back. Each shutdown completes with no crash evidence, and the log ends with `Number of memory leaks: 0` (crash evidence and allowed leaks as in R19).
  - When only the session with the switches ends with leaks, repeat it with no Program Output, after moving `branchOutputPrograms.json` and its `.bak` of both `BORegression` and `BORegression2` aside while OBS is closed. If that session also leaks, record it under "OBS" in "Observations" and do not fail the item; otherwise fail the item. Finally move the files back.

### M11 Context menu, rename, and removal — all

Do and Pass, with `Program 1` recording. Rename and remove Program Outputs only through the dock's menus: obs-websocket `SetInputName` and `RemoveInput` bypass the code under test. Hover over, click, and right-click the dock rows with the full-screen tools, with OBS frontmost: a hover needs the real pointer, and the background `app_*` tools cannot open context menus. To enter an existing Program Output's name in the rename dialog, copy it from that Program Output's own rename dialog (Command+A, Command+C, then Cancel), select the field's text with Command+A, and paste with Command+V: `pbcopy` from the shell does not reach OBS's clipboard. Type `Renamed Output` directly, and check that the field shows exactly that text before Return. Clear the field with Command+A and Delete. Select a dock row by left-clicking its Filter name, and judge a selection with the pointer off the dock's table: under the pointer, a row shows a hover color that can match the selection color.

- The "+" button shows a plus icon, or the text "+" where the theme has none; note which. Its tooltip is "Add Branch Output", and its menu has "Program", "Sources", and "Scenes". "Sources" lists the sources placed in `Main` and `Other` (`Media`, `Name`, `Quad`, `Tone440`, and the color source of `Other`) in name order, ignoring case, and no Program Output; "Scenes" lists `Main` and `Other`.
- "+" → "Sources" → `Media` adds a filter `Branch Output` to `Media`, logs `Added Branch Output filter 'Branch Output' to 'Media'`, opens `Media`'s filters dialog with the filter in it, and adds dock rows with `Branch Output` in the Filter column and `Media` in the Source column. Choosing `Media` again adds `Branch Output 2`. "+" → "Scenes" → `Other` adds a filter whose rows show `Other` in the Source column. Close each filters dialog. The new filters' rows stay "Inactive" (no Apply yet).
- Right-clicking a filter's row opens a menu with only "Open Settings", which opens that source's filters dialog. Close it, then delete the three filters.
- Row hover and selection: turn Replay Buffer on through obs-websocket and wait for "Buffering"; `Program 1` now has a recording row and a replay buffer row. Hover by moving the pointer without clicking, and judge it on a screenshot taken with the pointer there. Take the reference colors from OBS's Sources list (an item under the pointer, and a selected item with the pointer elsewhere) and compare only the fill colors: the list can add a focus border that the band lacks. Judge the colors loosely, since screenshots of different windows can differ slightly: a band passes when it stands out from the plain row background and has the reference's hue (grey for a grey reference, blue for a blue one), whatever its brightness or saturation. Fail a color only on a gross difference: no band, a band no different from the plain row background, or another hue (for example red or green where the reference is blue). Controls with their own look (the checkboxes, the Status cell's buttons, and the Reset button, a reset icon centered in the last column) stay drawn over the band.
  - Hover over the recording row's Filter name, its Status cell, and its Reset button in turn: each time, that row shows one band in the hover color across every column from Filter to Reset, with no gap, border, or rounded corner at their boundaries, and the replay buffer row shows none. On the Reset button, keep the pointer still until its tooltip shows (about 1 s) and judge again: the band stays. With the pointer below the last row, no row shows a band.
  - Select the recording row: only that row shows a band, in the selection color, and the replay buffer row is not selected. It stays selected after about 5 s (after the dock's refresh).
  - Press the Output column header, then press it again to reverse the order: the two rows swap places, and the selection follows the recording row. Press the Filter column header to return to the default order.
  - Select the replay buffer row: the selection moves to it. Left-click the empty area below the last row: no row is selected.
  - Left-clicking a row's own controls leaves the selection unchanged. Select the replay buffer row, then left-click the recording row's Reset button and its Source link (the dialog opens; close it with Cancel). Select the recording row, then turn the replay buffer row's checkbox off and on again: only the replay buffer stops and resumes. Do not left-click the eye icon here: it stops all of `Program 1`'s outputs.
- Right-clicking `Program 1`'s rows opens a menu with "Open Settings", "Rename", and "Remove", on every column of the recording row: the eye icon, the Source link, the Output link, the Status cell, and the Reset button. For each column, select the replay buffer row, right-click, check the menu, close it by clicking the dock's title bar (Escape from computer use does not close it), and check that the selection has moved to the recording row alone. The right-click has no other effect (the eye icon keeps its state, and no Finder window opens). "Open Settings" opens its properties dialog; close it with Cancel. Afterwards turn Replay Buffer off through obs-websocket.
- Rename: add a Program Output with "+" → "Program" and close its dialog with Cancel; it is `Program 2`. Choose "Rename" on its row: the dialog's field holds `Program 2`.
  - Enter `Program 1` and press OK: a warning says that another Program Output uses the name, the rename dialog reopens, and Cancel there leaves the name unchanged.
  - Clear the field and press OK: a warning asks for a name.
  - Enter `Renamed Output`, move the pointer off the dock without a click (for example onto OBS's preview), and press Return instead of clicking OK, both with the full-screen tools: its Filter cells show `Renamed Output`, its element in `branchOutputPrograms.json` has the name `Renamed Output`, and the log has `Renamed Program Output 'Program 2' to 'Renamed Output'`. Press the Filter column header to reverse the order, and move the pointer off the dock again: the two rows swap places, only `Renamed Output`'s row shows a band, in the selection color, and `Program 1`'s row shows none. A hover band that the dialog left behind would move with the reversal onto `Program 1`'s row, apart from the selection. Press the Filter column header again to return to the default order. After an OBS restart it is still `Renamed Output`.
- Removal: turn Stream Recording on in `Renamed Output`, and wait for "Recording". Its recording starts after `Program 1`'s, so stopping it first keeps the 30.1.2 ffmpeg-mux rule. Choose "Remove" on its row: the confirmation names `Renamed Output`, and No changes nothing. Choose "Remove" again and answer Yes: its rows leave the dock at once; within 5 s its recording stops and `Removed Program Output 'Renamed Output'` is logged; its file plays; `branchOutputPrograms.json` no longer lists it; and `Program 1` keeps recording to the same file.
- Removal while reconnecting: add a Program Output (`Program 2`), closing its dialog with Cancel, turn its Streaming on with one slot pointing at a local RTMP receiver, and wait for "Live". Stop the receiver, and once the row shows "Reconnecting", remove the Program Output with Yes: OBS stays responsive and does not crash, its rows leave the dock at once, and `Removed Program Output 'Program 2'` is logged within 30 s (the stop waits until 2 s after the last reconnect attempt, #214).
- Removal with the dialog open, in the dialog: add a Program Output (`Program 2`) and close its dialog with Cancel. Open the dialog again from its Source cell, set Resolution to "Half of canvas (50%)" without OK, and remove the Program Output from the dock with Yes: OBS's unsaved-changes prompt appears. Choose "Discard": the dialog closes, and `Removed Program Output 'Program 2'` is logged. On 30.1.2 the prompt comes back once after "Discard" (an OBS 30.1.2 bug, fixed in 30.2); choose "Discard" again.
- Undo, in the dialog: add a Program Output (`Program 2`), set Resolution to "Half of canvas (50%)" in its dialog, and press OK. Set Program to `Other` through obs-websocket, remove the Program Output with Yes, and choose Edit → Undo: Program returns to `Main`, OBS does not crash, and the Program Output returns neither to the dock nor to `branchOutputPrograms.json`. Between the OK and the Undo, do no other GUI operation that OBS can undo (for example adding or deleting a filter). Do not judge by `Null 'source' parameter`: libobs logs it only with `--verbose`.
- Quit OBS: the log ends with `Number of memory leaks: 0` (allowed leaks as in R19). Launch it again: the dock and `branchOutputPrograms.json` have only `Program 1`.

Afterwards make sure that `Main` is in Program and that `Program 1` is the only Program Output, removing any other that a failed step left.

### M12 Hotkeys — all

A Program Output's hotkeys are OBS frontend hotkeys: Settings → Hotkeys lists them at the top among OBS's own hotkeys, under no source or scene, and each description names the Program Output (for example `Enable 'Program 1'`, `Split 'Program 1' Recording File`). OBS saves an assignment made there in `[Hotkeys]` of the profile's `basic.ini`, and the plugin keeps a copy in `hotkey_bindings` of the Program Output's `settings` in `branchOutputPrograms.json`. obs-websocket `GetHotkeyList` lists their names, which end in `.{uuid}` with the Program Output's `uuid`. Judge a hotkey list in Settings → Hotkeys opened after the operation.

Do: with `Program 1` recording, set its Automatic File Splitting to "Only split manually" and its recording format to Hybrid MP4 (Matroska on 30.1.2, which lacks Hybrid MP4). Add `Program 2` with "+" → "Program", closing its dialog with Cancel, and through obs-websocket turn its Replay Buffer on and its Streaming and Stream Recording off.

Do and Pass:

- Registration by output type: Settings → Hotkeys lists, for `Program 1`, the Enable / Disable pair and the 6 recording items (Split, Pause, Unpause, Add chapter, Enable, Disable), and none for streaming or the replay buffer; for `Program 2`, the Enable / Disable pair and the 3 replay buffer items (Save, Enable, Disable). Then turn `Program 1`'s Streaming on with Stream Count 1, its slot pointing at a local RTMP receiver, and its Replay Buffer on, and wait for "Live" and "Buffering": `Program 1` now has 15 items, adding the all-streaming pair, the `Streaming 1` pair (no `Streaming 2`), and the 3 replay buffer items.
- Key actions: assign unused combinations, none of them R13's, to the 15 items of `Program 1` and to `Save 'Program 2' Replay Buffer`. Press each of `Program 1`'s as R13 does, with its streaming off while testing Pause and Unpause; R13's notes on Split and Add chapter apply. Each key acts on `Program 1` only, and the dock checkboxes update immediately: `Program 2` stays enabled and "Buffering", and saves no replay buffer file. `Program 2`'s Save key saves only its own replay buffer. Finish with `Program 1`'s streaming turned off by its Disable All Streaming key, and stop the receiver.
- Persistence: after each operation below, Settings → Hotkeys shows the 16 assignments.
  - Turn `Program 1`'s Stream Recording off through obs-websocket: its 6 recording items leave the list. Turn it on again. Do the same with Replay Buffer (3 items) and Streaming (the all-streaming and `Streaming 1` pairs; its streaming stays off from the key).
  - Rename `Program 2` to `Hotkey Output` with "Rename" in its row's menu, entering the name as M11 does: its 5 items name `Hotkey Output` (for example `Save 'Hotkey Output' Replay Buffer`).
  - Restart OBS.
  - Launch with `--safe-mode`, exit, and launch normally.
  - Switch to the profile `BORegression2` and back. While in `BORegression2`, no item naming a Program Output has a key.
  - Note the names that `GetHotkeyList` lists for `Program 1`, and quit OBS: `[Hotkeys]` in `BORegression`'s `basic.ini` has an entry with a non-empty `bindings` for each of them. Delete `hotkey_bindings` from `Program 1`'s `settings` in `BORegression`'s `branchOutputPrograms.json`, and launch OBS: the assignments come back from `[Hotkeys]`. Restart the receiver and press `Program 1`'s Enable All Streaming key: its slot goes "Live".
- Removal, in the dialog: note `Hotkey Output`'s `uuid` in `branchOutputPrograms.json`. Open its dialog from its Source cell, set Resolution to "Half of canvas (50%)" without OK, and remove `Hotkey Output` from the dock with Yes as M11 does: OBS's unsaved-changes prompt appears. Keep it open until `Removed Program Output 'Hotkey Output'` is logged; `GetHotkeyList` then lists no name ending in its `uuid`. Choose "Save": `GetHotkeyList` still lists none of its names, and Settings → Hotkeys has none of its items, while `Program 1`'s 15 keep their keys. Do not judge by `Hotkey Output: Program Output updated`: whether Save logs it depends on the OBS version and timing.

Afterwards turn `Program 1`'s Streaming, Replay Buffer, and Automatic File Splitting off and its recording format back to its value before M12 through obs-websocket, stop the receiver, and make sure that `Program 1` is the only Program Output.

### M13 Direct video path — all

A Program Output encodes OBS's own video as is (the direct path) when that yields the same frames as its own view: no crop, its output resolution equal to OBS's, and, when OBS scales (its output resolution differs from its base resolution), `downscale_filter` empty or equal to OBS's Downscale Filter. Otherwise it renders the program through its own view (the proxy path).

- Each build of `Program 1`'s video input (a start from standby, a restart for a setting change, a restart for a change of OBS's video settings) logs `[osi-branch-output] Program 1: Video path: direct ({width}x{height})` or `... Video path: proxy ({width}x{height})`. Judge the path by the newest such line after each operation.
- A setting change logs one `Settings change detected, Attempting restart` line and one new `Video path` line, and starts a new recording file. Between those two lines, libobs logs `NV12 texture support` or `P010 texture support` for the view before a proxy line, and neither before a direct line.
- Change the settings through obs-websocket `SetInputSettings`:
  - Crop: `crop_type` `relative` with `crop_rel_right` 640, `crop_rel_bottom` 360, and `crop_rel_top` and `crop_rel_left` 0 (the top-left quadrant); `crop_type` `none` to remove it.
  - Resolution: `resolution` `half`, `canvas`, `output`, or `custom` with `custom_width` 1280 and `custom_height` 720 in the same request.
  - Downscale filter: `downscale_filter` `lanczos`, or `bilinear` when OBS's Downscale Filter is Lanczos; an empty string to restore.
  - Frame rate divider: `fps_divider` 2; 1 to restore.
- Change OBS's video settings only in Settings → Video: obs-websocket `SetVideoSettings` resets the video from another thread, which is left to #216.

Do and Pass, starting with `Program 1` recording at its defaults (x264, Stream Recording only, Resolution "Output (Stretch to fit)", no crop) and every OBS output stopped. In steps 1–5, keep each setting about 5 s before the next change.

1. The newest `Video path` line is `direct (1280x720)`. The recording is 1280x720 and shows the four quadrants of `quad.png`.
2. Set the top-left crop: `proxy (1280x720)`, and the output is filled with the top-left quadrant's color. Remove the crop: `direct (1280x720)`.
3. Set Resolution `half`: `proxy (640x360)`, and the recording is 640x360. Then `canvas`: `direct (1280x720)`. Then Custom 1280x720: `direct (1280x720)`. Then `output`: `direct (1280x720)`.
4. Set `fps_divider` 2: `direct (1280x720)`, and the recording runs at 15 fps. Set it back to 1.
5. Set `video_encoder` to R01's hardware encoder: `direct (1280x720)`, and the recording plays and shows the four quadrants. Set it back to x264. Without a hardware encoder on the machine, SKIP this step and note it.
6. Disable `Program 1` with the eye icon in its Filter cell. Once its rows are "Inactive", start OBS's streaming (to the profile's local RTMP receiver, writing to a file) and its recording back to back through obs-websocket, and wait 5 s: a recording started even a fraction of a second later waits for the next keyframe of the encoder it shares with the stream and falls short of frames. Enable and disable `Program 1` with its eye icon three times, about 5 s each, ending disabled with every row "Inactive". After 5 s more, stop OBS's recording and streaming. On 30.1.2, keep this order for the ffmpeg-mux rule: OBS's recording starts first and stops after every `Program 1` recording.
   - Each enable logs `direct (1280x720)`, and each of `Program 1`'s recordings plays.
   - OBS's recording plays from start to stop without a break: ffprobe counts at least 99% of (its duration × 30) video frames, no two consecutive frames before its last frame are more than 0.1 s apart, and its audio carries both the 1 kHz and the 440 Hz tone. The receiver's file meets the same criteria. A larger gap before a file's last frame falls at the stop of OBS's outputs, away from the eye icon operations: record it in "Observations" instead of failing the case.
   - The log has no `Output 'simple_stream': Number of dropped frames due to insufficient bandwidth/connection stalls` line.
7. Enable `Program 1` again. While it records, start OBS's recording, and stop it after 10 s. On 30.1.2, keep this order for the ffmpeg-mux rule: OBS's recording, started later, stops first.
   - OBS's recording meets step 6's criteria.
   - `Program 1` does not restart (no new `Video path` or `Settings change detected` line), and its recording continues in the same file.

From the start of OBS's outputs in step 6 until every row is "Inactive" in step 8, the log has no libobs line `Video stopped, number of skipped frames due to encoding lag`. libobs logs it, after skipped frames only, when the last encoder leaves OBS's video: at the end of step 6 and in step 8. When it appears, repeat step 6 with `Program 1` kept disabled throughout. If it appears there too, record it under "OBS" in "Observations" instead of failing the case; otherwise the case fails.

8. Set the dock Interlock to "Always OFF". Once every row is "Inactive", set Settings → Video → Output (Scaled) Resolution to 640x360, note the Downscale Filter, and press OK. Set Interlock back to "Always ON": `direct (640x360)`, and the recording is 640x360.
9. Set `downscale_filter` as listed above: `proxy (640x360)`. Set it back to empty: `direct (640x360)`.
10. Set Resolution `canvas`: `proxy (1280x720)`. Set it back to `output`: `direct (640x360)`.
11. Restore 1280x720 as step 8 does: `direct (1280x720)`.

Edge: OBS's video settings changed while a stream reconnects. Start the receiver again only after the new `Video path` line: until the stop completes, the slot keeps reconnecting even after `OBS video settings changed, Attempting restart`, and a reconnect then would start the encoder on the freed video (#216).

12. Direct path: turn `Program 1`'s Stream Recording off and its Streaming on with one slot pointing at a local RTMP receiver. Keep OBS's outputs and every other Branch Output stopped: OBS refuses a video reset while any encoder runs. Once the slot is "Live" with `direct (1280x720)`, stop the receiver. Once the row shows "Reconnecting", set Settings → Video → Output (Scaled) Resolution to 640x360 and press OK.
    - The video settings can be edited, and OK accepts the change.
    - Within 30 s, `OBS video settings changed, Attempting restart` and then `Video path: direct (640x360)` are logged. OBS stays responsive and does not crash.
    - Started after that line, the receiver gets 640x360 video, and the slot goes "Live".
13. Proxy path: in that state (OBS's output resolution 640x360, the slot "Live"), set the top-left crop: `proxy (640x360)`. Restart the receiver for the new connection. Once the slot is "Live", stop the receiver. Once the row shows "Reconnecting", set Output (Scaled) Resolution back to 1280x720 and press OK.
    - Within 30 s, `OBS video settings changed, Attempting restart` and then `Video path: proxy (1280x720)` are logged. OBS stays responsive and does not crash.
    - Started after that line, the receiver gets 1280x720 video filled with the top-left quadrant's color, and the slot goes "Live".

Afterwards remove the crop, turn `Program 1`'s Streaming off and Stream Recording on, and stop the receiver. Make sure that OBS's output resolution is 1280x720 and that `Program 1` has its defaults (no crop, `output`, empty `downscale_filter`, divider 1, x264).

Under "Not covered", list: whether a hardware encoder took the GPU texture path (the plugin cannot tell); a video reset during a stream's first connection, a reconnect that succeeds between a reset and the plugin's next check (1 s), and a reset that keeps the video settings (#216).

### M14 File name override scripts, Python — latest

Do and Pass: M09's Do, steps 3–7, and Afterwards with the `.py` scripts and Python set up as R18 says (SKIP as R18 does). Leave out the filter on `Media` and its items, including the unchecking and checking of `Program 1`'s recording row: `Program 1` records throughout, so the choice in step 3 splits its recording to a file starting with `M14`. Set `Name`'s `text` to `M14`, reading `M09` in M09's items as `M14` and `M09B` as `M14B`. Load both scripts as R17's Do does, and in step 3 also set the recording script's text source to `Name`. Before step 5, switch the scene collection to `BORegression2` and back:

- While in `BORegression2`, `Program 1` records to a file in its own format: the scripts unload with their collection and clear the override.
- Back in `BORegression`, `Program 1` records to a file starting with `M14` (a default-format file before it, as in M09's step 4).
- Step 5 then still passes: OBS reuses the loaded Python module when it loads the scripts again, keeping the state of their previous unload.
