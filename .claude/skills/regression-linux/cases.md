# Regression cases (Linux)

Issue numbers name the bug an item guards against. "Within 5 s" limits are pass criteria; a longer wait is a failure. The dock refreshes statuses every 2 s; judge a status after the next refresh.

## Fixture

- Media in `<work>/media/`, generated with ffmpeg:
  - `quad.png`: 1280x720, four solid-color quadrants in distinct colors. `quad-small.png`: the same at 640x360.
  - `quad-1k.mp4`: `quad.png` as 30 fps H.264 video with a 1 kHz sine tone in AAC, 10 s, encoded with an H.264 encoder that `ffmpeg -encoders` lists (Fedora's ffmpeg-free has libopenh264, not libx264).
  - `tone440.wav`: 440 Hz sine, 10 s.
- Profiles `BORegression` and `BORegression2`: Simple output mode, x264, base and output resolution 1280x720 at 30 fps, recording path `<work>/rec/obs-rec`, replay buffer enabled, streaming service Custom pointing at a local RTMP receiver.
- Scene collection `BORegression`:
  - Scene `Main`: media source `Media` (`quad-1k.mp4`, loop, filling the canvas), image source `Quad` (`quad.png`), media source `Tone440` (`tone440.wav`, loop), text source `Name` (`text_ft2_source_v2`).
  - Scene `Other`: one color source.
- Scene collection `BORegression2`: one scene with one source that has a Branch Output filter.

Defaults unless a case says otherwise: one Branch Output filter on `Media` with x264, Save Path `<work>/rec/bo-rec`, dock Interlock "Always ON", every dock checkbox on, and `Main` in Program. Remove filters left by earlier cases when they would affect the result.

Press the properties' Apply once after adding a filter in any case (for example the filter on the scene `Main` in R09): a newly added filter, and a filter loaded with every output type off, starts no output until then, even when obs-websocket sets its settings and enables it. Settings changed through obs-websocket after that take effect.

The hardware encoder in R01 and R08 is an NVENC or VAAPI encoder that the profile's encoder list offers. On Xvfb OBS renders with llvmpipe, so these encoders take frames from system memory; their GPU texture path is not covered.

## R01 Startup and new filter

Do: delete `recently.json` from `<config>/plugin_config/osi-branch-output/` if it exists, and set the profile's streaming encoder to the hardware encoder. Add a Branch Output filter to `Media`, turn Stream Recording on, and Apply. Afterwards set the profile encoder back to x264, switch the filter to x264 in its properties (a switch through obs-websocket does not reset a preset that x264 does not list), and Apply (a new filter copies the last applied filter's settings from `recently.json`). Then record Matroska with it (Hybrid MP4 bypasses ffmpeg-mux, where #208 failed).

Pass:

- The Docks menu has "Branch Output Status", and it lists the new filter's rows.
- The new filter's Video Encoder is the profile's hardware encoder, and the recording it writes plays. With no hardware encoder offered, use x264, leave out the switch to x264 with its pass item, and note both.
- After the switch to x264 (#208): the properties show the x264 preset `veryfast`, the log has the x264 encoder's `preset: veryfast` and no `Invalid preset` line, and the Matroska recording plays.

## R02 Streaming

Do: turn Streaming on with Stream Count 2, each slot pointing at its own local RTMP receiver, and Apply.

Pass:

- Both receivers get video and the 1 kHz tone; the dock shows both slots "Live" with growing Sent Size and Bitrate.
- Unchecking slot 2 in the dock stops only slot 2; checking it again resumes it.
- Stopping receiver 1 turns slot 1 to "Reconnecting"; restarting the receiver brings it back to "Live" with data arriving.

Edge:

- Disabling the filter (eye icon) while slot 1 is "Reconnecting": OBS stays responsive and every row turns "Inactive" within 5 s.
- Slot 1 set to an SRT listener URL (`srt://127.0.0.1:{port}?mode=listener`) with no caller: enabling and then disabling the filter turns the rows "Inactive" within 5 s, and OBS does not freeze. Enabling it again and connecting an ffmpeg SRT caller delivers data without a crash.

## R03 Recording

Do: Streaming off, Stream Recording on, a file name format containing `%1` and `%2`. Record Matroska, then Hybrid MP4. Then turn "Use profile's recording path" on and record once more.

Pass:

- Each file's name expands `%1` to the source name and `%2` to the filter name.
- ffprobe: 1280x720 video, one audio stream carrying the 1 kHz tone, duration short of the time between the recording's start and stop log lines by at most OBS's own shortfall plus 0.2 s. Measure OBS's own shortfall once with an OBS recording of about 10 s (the time between `==== Recording Start` and `==== Recording Stop` minus the file's duration): on Xvfb it can exceed 1 s.
- With "Use profile's recording path", the file is written to the profile's recording path instead of the Save Path.

## R04 Recording control

Do: record Matroska with "Generate File Name without Space" on and a file name format containing spaces. First use "Split by Size" with a small size (for example 2 MB). Then use "Only split manually" and operate the dock: the row's Split / Pause / Unpause buttons and the Split All / Pause All / Unpause All buttons. On Hybrid MP4, also add a chapter from the dock.

Pass:

- Split by size produces several consecutive playable files, and no file name contains a space.
- Each manual split starts a new file.
- Pause shows "Paused" and Unpause returns to "Recording"; the file's duration excludes the paused time.
- Hybrid MP4: the chapter appears in `ffprobe -show_chapters`.

Edge: split within the first second after the recording starts. No empty file results, and the first file's audio decodes (#191).

## R05 Replay buffer

Do: on two filters, turn Replay Buffer on with Maximum Replay Time 10 s and "Show estimated memory usage" on, and set the video encoder's keyframe interval to 1 s. Wait more than 10 s, save with one row's Save button, then with "Save All Replay Buffers". Tick "Show estimated memory usage" in the properties: it is display-only, reset to off when the settings are saved, so obs-websocket cannot turn it on.

Pass:

- The dock shows "Buffering", and the properties show an estimated memory usage in MB.
- Each save writes one file per saved filter, about 10 s long and at most 11 s (a save keeps whole keyframe intervals, so it can exceed the maximum by one interval), with video and audio, and logs `Replay buffer saved`.
- The saved file names expand `%1` and `%2` as in R03.

## R06 Audio sources

Do: record about 10 s with each audio setting: Custom Audio Source off (filter audio); custom source `Tone440`; Master Audio track 1; No Audio; Multitrack Audio with track 1 = `Tone440`, track 2 = filter audio, and one track Disabled.

Pass (check the audio stream count and each stream's dominant frequency):

- Filter audio: 1 kHz only. `Tone440`: 440 Hz only. Master Audio: both tones. No Audio: one silent audio stream.
- Multitrack: one audio stream per enabled track with the expected tone; the disabled track has no stream.

## R07 Video output

Do: record about 5 s with each setting: Resolution "Half of source (50%)"; Custom 640x360 with Frame Rate Divider 1/2; Relative crop keeping only the top-left quadrant; Absolute crop selecting the bottom-right quadrant. With the properties open, turn "Preview Cropping Rect" on, then close the properties.

Pass:

- Output sizes: 640x360 each. The divider case runs at half the canvas frame rate (15 fps).
- Each crop output is filled with the color of the selected quadrant.
- The crop rectangle is drawn over the source while the preview option is on and the properties are open, and disappears when they close.

## R08 Filter input mode

Do: set Video Source to "Filter Input (Experimental)" on the filter on `Media`. Add a Color Correction filter that visibly changes the colors, first above Branch Output, then below it. Record each order with x264 and with the hardware encoder. Then add a Branch Output filter in Filter Input mode directly to `Quad` with no other filter on it, and record.

Pass:

- The effect is in the output when the Color Correction filter is above Branch Output, and absent when it is below.
- Both encoders produce correct 1280x720 content. With no hardware encoder offered, note it.
- `Quad`: the output shows the image, not black, and the OBS preview still shows `Quad` (#170).
- No "Branch Output Proxy" source appears in the Sources list or in any source picker.

## R09 Blanking and mute

Do: turn on "Blank output when source is not in Main Output" and "Mute audio while blanked", with Multitrack Audio track 1 = Master Audio and track 2 = filter audio. Record while switching Program from `Main` to `Other` and back. Then, with `Other` in Program, disable and re-enable the filter.

Pass:

- While `Other` is in Program: frames are black, every audio track is silent, and the dock status ends with "(Blank+Mute)". Back on `Main`: the picture and the tones return.
- Enabling the filter while `Other` is in Program blanks from the first frame.
- Studio Mode, with `Main` only in Preview and `Other` in Program: the output is blanked. Transitioning `Main` to Program ends the blanking.
- Studio Mode, with a blanking Branch Output filter on the scene `Main` itself: not blanked while `Main` is in Program, blanked while `Main` is only in Preview. Judge this filter by video: its filter audio is always silent, because a scene passes no audio filter.

The `BranchOutputFilter destroyed` line each Studio Mode transition logs (the filter on Studio Mode's discarded scene copy) is not a failure.

## R10 Interlock modes

Do: with the filter's streaming slot pointing at a local RTMP receiver (an SRT listener adds up to 5 s to each stop) and recording on, set each dock Interlock value in turn (Always ON, Streaming, Recording, Streaming or Recording, Replay Buffer, Virtual Cam as described below, Always OFF), and start and stop the matching OBS outputs.

Pass:

- The filter's outputs run exactly while the interlock condition holds (Always ON: always; Always OFF: never), starting and stopping within 5 s.
- An unchecked dock checkbox keeps its output off even while the condition holds.

Virtual Cam: test the value only when all conditions below hold; otherwise SKIP it, give the unmet condition as the reason, and do not start the virtual camera.

- The `v4l2loopback` kernel module is loaded (`/sys/module/v4l2loopback` exists). Without it OBS runs `pkexec modprobe v4l2loopback`, which asks for the user's password on their desktop.
- The user can read and write the loopback device, the `/dev/videoN` whose `N` is listed under `/sys/devices/virtual/video4linux/` (its ACL may grant only the login screen's user).
- No process has that device open (`lsof /dev/videoN`): another application may be reading it.

When the virtual camera still fails to start, SKIP the value with the failure as the reason.

## R11 Individual interlock

Do: set Interlock to "Individual", with streaming, recording, and replay buffer on in the filter. Start and stop OBS streaming, recording, and replay buffer one at a time.

Pass:

- Each Branch Output type runs exactly while its OBS counterpart runs (streaming, recording, replay buffer), independently of the others.
- The dock checkboxes turn single outputs off and back on while their counterpart runs.

Edge: with only OBS recording running, change a filter setting and Apply. Only the recording restarts: no Branch Output stream goes live, no extra recording file of about 1 s appears, and no replay buffer starts (#189).

## R12 Status dock

Do: run outputs on three filters (on `Media`, on `Quad`, and on the scene `Main`).

Pass:

- "Deactivate All" and "Activate All" disable and enable every filter, and the filters' eye icons follow.
- A row's Reset sets only that row's dropped frames back to 0, also after one filter is deleted and another added. "Reset All" does so on every row (#182). Sent Size returns to the cumulative total at the next refresh (#188).
- Clicking a recording or replay buffer row's folder cell appends a line with its save folder's `file://` URL to `<work>/xdg-open.log`.
- The Interlock value is kept per profile: set different values in `BORegression` and `BORegression2`; switching profiles shows each profile's own value.

## R13 Hotkey actions

Do: in Settings → Hotkeys, assign unused combinations (for example Ctrl+Alt+Shift+{key}) to one hotkey of each kind: the filter's Enable / Disable, Enable / Disable All Branch Outputs, the filter's all-streaming and slot-1 Enable / Disable, recording Enable / Disable, replay buffer Enable / Disable, Split, Pause, Unpause, Add chapter, Save replay buffer, and the "all" variants of split, pause, unpause, chapter, and save. Press each, with the filter's streaming off while testing Pause and Unpause (pausing is refused while the filter streams). After a Split key, wait for the new file before pressing a Pause key: a split still pending at Pause waits until after Unpause. Keep the assignments for R14.

Pass:

- Each key acts on its target only, and the dock checkboxes update immediately.
- The "all" hotkeys also work with the dock closed.
- Add chapter acts only on Hybrid MP4 recordings.

## R14 Hotkey persistence

Do and Pass: after each operation below, Settings → Hotkeys shows the R13 assignments.

- Turn Stream Recording off, Apply, turn it on, Apply: the recording group has all 6 items (Split, Pause, Unpause, Add chapter, Enable, Disable) with their keys. Do the same for Replay Buffer: 3 items (Save, Enable, Disable).
- Rename the filter: keys kept, and the descriptions show the new name.
- Delete the filter, then Undo: keys restored, and the outputs run again.
- Restart OBS: keys kept.
- Launch with `--safe-mode`, exit, and launch normally: keys kept.
- Switch to `BORegression2` and back: keys kept, including the "all" hotkeys.

## R15 Apply semantics

Do and Pass:

- Change the resolution in the properties without pressing Apply: the running outputs continue and the dock does not change. Press Apply: the outputs restart once with the new resolution (a single `Settings change detected, Attempting restart` line).
- While streaming runs, turn the Replay Buffer group on without Apply: no replay buffer starts. Apply: the outputs restart once, the replay buffer shows "Buffering" afterwards, and OBS stays responsive (#184).

## R16 Source availability

Do and Pass, with a filter on `Quad` and recording on:

- Point `Quad` at `quad-small.png`: the output restarts at 640x360. With "Don't reset output when source resolution changes" on, it does not restart.
- With "Suspend recording when source is not available" on and recording only: renaming `quad.png` away shows "Paused"; restoring it returns to "Recording". With streaming also on: renaming away shows "Pending" for the recording, and restoring it restarts the recording.
- While the recording is "Pending", turn off every output type and Apply: the rows become "Inactive", and `Settings change detected` does not repeat every second (#194).
- Delete `Quad` from the scene: its outputs stop. Undo: the source returns and its outputs run again.

## R17 File name override scripts, Lua

Do: load `recording-filename-from-text.lua` and `replay-buffer-filename-from-text.lua` from `<config>/plugins/osi-branch-output/data/scripts/`: with OBS closed, append `{"path": "{absolute path}", "settings": {}}` for each to `modules.scripts-tool` in `<config>/basic/scenes/BORegression.json`. In Tools → Scripts, set each script's text source to `Name` and its filter to the one on `Media`. Record with "Only split manually".

Pass:

- Each script's filter list shows every Branch Output filter of the collection.
- The recording file name follows the text. Changing the text during the recording splits to a new file with the new name (the script applies one change per 30 s per distinct text).
- The next replay buffer save uses the text-based name.
- A text change while the recording is paused takes effect only after Unpause.
- Removing the scripts restores the filter's own file name format, splitting the running recording to a new file in that format.

Edge: with the recording script loaded and the filter recording with splitting enabled, restart OBS. The first recording file after startup is not empty and its audio decodes (#191).

## R18 File name override scripts, Python

Do and Pass: R17's Do, Pass, and Edge with the `.py` scripts. OBS runs them on the Python it was built against; SKIP when the log shows no `[obs-scripting]: Loaded python script:` line for them.

## R19 Lifecycle and shutdown

Do and Pass:

- Toggle the filter's eye icon five times quickly while every output runs: the filter ends in the last state, and no failure line is logged.
- Switch to the scene collection `BORegression2` and back, once with outputs running and once with them stopped: no crash, and the dock lists only the current collection's filters.
- Quit OBS once with outputs running and once with them stopped, each from a session launched for this item: shutdown completes, `coredumpctl list obs --since {launch time}` shows no new entry, and the log ends with `Number of memory leaks: 0`.

## Main output

A main output is a Branch Output whose input is OBS's program output instead of a source. It belongs to a profile and is saved in `branchOutputPrograms.json` in the profile's folder, `<config>/basic/profiles/{profile}/`.

- Add one with the "+" button at the left end of the dock's bottom button row, choosing "Main Output" in its menu. It creates `Main Output {N}`, with the smallest number that no main output of the profile uses, and opens its properties in OBS's standard properties dialog. Its dock rows show the name in the Filter column and `Main Output` in the Source column; clicking the Source cell opens the dialog.
- Change its settings through obs-websocket `SetInputSettings` with `inputUuid` set to its `uuid` in `outputs` of `branchOutputPrograms.json`, read from the file each time. The change applies at once, like the dialog's OK, with no Apply. A main output is a private source: obs-websocket finds it by UUID, not by name. Type no text or numbers into the dialog, so that every platform uses the same means.
- Use the dialog only in the steps a case marks "in the dialog", where the case checks the dialog itself. There too, operate only lists, checkboxes, and buttons.
- Change no setting through obs-websocket while a main output's dialog is open: the dialog's Cancel restores and reapplies the settings it opened with.
- Keep the default name, except for the renames in M11 and M12.
- Close with Cancel a dialog opened only to look.
- Before M01, reduce the program picture to the four quadrants of `quad.png`, because state left by the R cases skews the color checks:
  - Delete every filter the R cases left on the sources and scenes of `BORegression`, of any kind (Branch Output, Color Correction, and so on).
  - Point `Quad` back at `quad.png`.
  - Hide `Name` (obs-websocket `SetSceneItemEnabled`).
- Defaults unless a case says otherwise: no filter in `BORegression` other than those the case adds; one main output, `Main Output 1` from M01, with x264, Stream Recording on, and Resolution at its default "Output (Stretch to fit)"; dock Interlock "Always ON", every dock checkbox on, `Main` in Program, and Studio Mode off.
- Keep a main output's recordings and replay buffer saves in its default folder, the profile's recording path, where OBS's own recordings also go, and tell the files apart by name. The default file name format is `%1 %2 {the profile's format}`, where `%1` expands to `Main Output` and `%2` to the main output's name.
- M02–M12 use M01's main output. When it is missing (for example on a resume in a rebuilt work folder), first create it as M01's Do does, with x264.
- To remove a main output (to redo a case, or to resume R cases on the same version), choose "Remove" in the menu that right-clicking one of its dock rows opens, and answer Yes. Its rows leave the dock at once, and `Removed main output '{name}'` is logged once its outputs have stopped. While OBS is closed, delete its element from `outputs` in `branchOutputPrograms.json` instead. To delete the whole file, also delete `branchOutputPrograms.json.bak` and any `branchOutputPrograms.json.tmp`: the plugin loads the `.bak` when it cannot read the file. The R cases assume that the profile has no main output.

### M01 Add and configure

Do: delete `recently.json` as in R01, and set the profile's streaming encoder to the hardware encoder of R01. Choose "+" → "Main Output" in the dock, check the items of the dialog it opens (turn Custom Audio Source on to see the audio source list, and off again), turn Stream Recording on in the dialog, and close it with OK without pressing the dialog's Apply. Record about 10 s. Then set the profile encoder back to x264, set the main output's `video_encoder` to x264, and record again.

Pass:

- The dock lists `Main Output 1`'s rows, with `Main Output 1` in the Filter column and `Main Output` in the Source column.
- The recording starts within 5 s of the OK, and the dock's recording row shows "Recording" (unlike a filter, without the dialog's Apply).
- The dialog has no "Video Source" list, no "Blank output when source is not in Main Output" or "Mute audio while blanked", no "Suspend recording when source is not available" in Stream Recording, and no "Filter Audio" among the audio sources. Resolution defaults to "Output (Stretch to fit)".
- The new main output's Video Encoder is the profile's hardware encoder. Without a hardware encoder, use x264 and note it, as in R01.
- Both recordings play, are 1280x720, and show the four quadrants of `quad.png` in the program's layout. Each has one audio stream carrying both the 1 kHz and the 440 Hz tone (the default audio is master track 1).
- The file names expand `%1` to `Main Output` and `%2` to `Main Output 1`.
- `branchOutputPrograms.json` in the profile's folder lists `Main Output 1`.
- OBS's source add menu (the "+" below the Sources list) has no "Branch Output Main Output" or "Branch Output Program Proxy", and no source derived from the main output appears in the Sources list or in any source picker (as R08 checks for "Branch Output Proxy").

### M02 Apply semantics

Do and Pass, every step in the dialog, with `Main Output 1` recording. Count restarts by the `Settings change detected, Attempting restart` lines.

- Open the dialog and press Cancel without a change: it closes without OBS's unsaved-changes prompt ("There are unsaved changes. Do you want to keep them?"), and no restart.
- Set Resolution to "Half of canvas (50%)" and press Cancel: no restart, and on reopening Resolution is "Output (Stretch to fit)".
- Set Resolution to "Half of canvas (50%)" and press the dialog's Apply: one restart with the dialog still open, and the recording is 640x360. Then press OK: no restart.
- Open the dialog and press OK without a change: no restart (unlike a filter's Apply without a change, #178).
- Open the dialog, set Resolution back to "Output (Stretch to fit)", press the dialog's Apply (one restart), and then Cancel: one more restart, back to 640x360, the settings the dialog opened with. On reopening Resolution is "Half of canvas (50%)". Finally set "Output (Stretch to fit)" and press OK: one restart, back to 1280x720.
- Turn Replay Buffer on without applying: no replay buffer starts. Press OK: one restart, then "Buffering". Afterwards turn Replay Buffer off through obs-websocket.

### M03 Streaming, recording, and replay buffer

Do: turn Streaming on with Stream Count 2 (each slot pointing at its own local RTMP receiver), Stream Recording on, and Replay Buffer on with Maximum Replay Time 10 s and the video encoder's keyframe interval 1 s. Wait more than 10 s, then save with the replay buffer row's Save button.

Pass:

- Both receivers get 1280x720 video and both tones. The dock shows both slots "Live" with growing Sent Size and Bitrate, the recording "Recording", and the replay buffer "Buffering".
- Unchecking slot 2 in the dock stops only slot 2; checking it again resumes it.
- The recording plays, and its duration meets R03's criterion.
- The save meets R05's criteria: one file, about 10 s long and at most 11 s, with video and audio, and `Replay buffer saved` logged.

Afterwards turn Streaming and Replay Buffer off, keeping Stream Recording on.

### M04 Program output, transitions, and Studio Mode

Do: `Main Output 1` records only. Through obs-websocket, set the transition to Fade with a duration of 2000 ms. While recording, switch Program from `Main` to `Other` and back to `Main`. Then turn Studio Mode on with `Main` in Program and `Other` in Preview, wait 3 s, open the dialog from the Source cell, check its preview, close it with Cancel, and transition `Other` to Program. Finally turn Studio Mode off, set the transition duration back to 300 ms, and put `Main` back in Program.

Pass:

- While `Main` is in Program the output shows the four quadrants, and while `Other` is in Program the solid color of `Other`. Frames during each 2 s fade blend the two (neither pure color).
- Studio Mode: while `Other` is only in Preview the output shows `Main`; after the transition it shows `Other`.
- The dialog's preview opened in Studio Mode shows the Program (`Main`).
- No dock status ends with "(Blank)" or "(Blank+Mute)".

### M05 Video output

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

### M06 Audio sources

Do: record about 10 s with each audio setting, Custom Audio Source on: source `Tone440`; Master Audio track 1; No Audio; Multitrack Audio with track 1 = Master Audio track 1, track 2 = `Tone440`, and track 3 = Disabled. Keep recording with the Multitrack setting, switch the scene collection to `BORegression2`, wait about 10 s, and switch back. Finally turn Custom Audio Source off.

Pass (check the audio stream count and each stream's dominant frequency, as in R06):

- `Tone440`: 440 Hz only. Master Audio: both tones. No Audio: one silent audio stream.
- Multitrack: one audio stream per enabled track (both tones; 440 Hz); the Disabled track has no stream.
- The recording file started while `BORegression2` is loaded has one audio stream (Master Audio), and the log shows `Ignore audio source for track 2`. The file started after returning to `BORegression` has two streams again, the second at 440 Hz.

### M07 Status dock and interlock

Do: add a Branch Output filter to `Media` with only Streaming on, pointing at its own local RTMP receiver, and Apply. On `Main Output 1`, turn Streaming on with one slot pointing at another receiver, Stream Recording on, Replay Buffer on, and Automatic File Splitting set to "Only split manually" (without a split setting, Split All does nothing).

Pass:

- Disabling `Main Output 1` with the eye icon in its Filter cell turns its rows "Inactive" within 5 s while the filter keeps running. Enabling it resumes its outputs.
- "Deactivate All" and "Activate All" stop and resume both the filter and `Main Output 1`, and their eye icons follow.
- "Split All" splits `Main Output 1`'s recording to a new file, and "Save All Replay Buffers" saves `Main Output 1`'s replay buffer.
- A `Main Output 1` row's Reset sets only that row's dropped frames back to 0.
- Clicking the folder cell of `Main Output 1`'s recording or replay buffer row appends a line with its save folder's `file://` URL to `<work>/xdg-open.log`, judged as in R12.
- Clicking a `Main Output 1` Source cell opens its dialog, OBS's standard properties dialog. Close it with Cancel.
- With Interlock set to Streaming, start and stop OBS's streaming: `Main Output 1`'s outputs run only while OBS streams, starting and stopping within 5 s, and OBS's own stream reaches the profile's receiver with video and audio.

Afterwards set Interlock back to "Always ON", delete the filter on `Media`, and turn `Main Output 1`'s Streaming, Replay Buffer, and Automatic File Splitting off.

### M08 Persistence

Do and Pass, with Stream Recording and Replay Buffer on in `Main Output 1`:

- Uncheck the replay buffer row in the dock, then restart OBS: `Main Output 1` is listed with its settings kept (Stream Recording and Replay Buffer on, x264), its recording runs again, and its replay buffer stays unchecked, without "Buffering". Check it again.
- Disable `Main Output 1` with the eye icon in its Filter cell, then restart OBS: it is listed disabled and runs nothing. Enabling it starts its outputs.
- While it records, switch the profile to `BORegression2`: `Main Output 1`'s rows leave the dock, its recording stops, and the file plays. In `BORegression2`, set the dock Interlock to "Always ON" and choose "+" → "Main Output" twice, closing each dialog with Cancel without a change: each closes without OBS's unsaved-changes prompt (M02), and `Main Output 1` and `Main Output 2` appear. Turn only Replay Buffer on in `BORegression2`'s `Main Output 1`.
  - Switch back to `BORegression`: only `BORegression`'s `Main Output 1` is listed, with its settings kept, and its recording runs again.
  - Switch to `BORegression2` again: its two main outputs are listed, `Main Output 1` shows "Buffering", and `Main Output 2` runs nothing.
  - Each profile's `branchOutputPrograms.json` lists only that profile's main outputs (two in `BORegression2`).
  - Finally switch back to `BORegression`.

Afterwards turn Replay Buffer off.

### M09 Script filter list

Do: add a Branch Output filter to `Media` and Apply. Load `recording-filename-from-text.lua` as R17's Do does, and open that script's filter list in Tools → Scripts.

Pass: the list has the filter on `Media` and no `Main Output 1`.

Afterwards remove the script and delete the filter on `Media`.

### M10 Lifecycle and shutdown

Do and Pass, where "outputs running" means `Main Output 1`'s Streaming, Stream Recording, and Replay Buffer all running:

- Toggle the eye icon in `Main Output 1`'s Filter cell five times quickly while its outputs run: it ends in the last state, and no failure line is logged.
- Switch the scene collection to `BORegression2` and back, once with outputs running and once with `Main Output 1` disabled by its eye icon: no crash, and after each switch the dock lists each of `Main Output 1`'s rows once (they may disappear during the switch). With outputs running, they stop during the switch and run again after it: each switch starts a new recording file, and the receiver gets data again.
- Quit OBS from three sessions, each launched for this item: one with outputs running, one with them stopped, and one with outputs running after switching the profile to `BORegression2` and back and then the scene collection to `BORegression2` and back. Each shutdown completes with no crash evidence, and the log ends with `Number of memory leaks: 0` (crash evidence and allowed leaks as in R19).
  - When only the session with the switches ends with leaks, repeat it with no main output, after moving `branchOutputPrograms.json` and its `.bak` of both `BORegression` and `BORegression2` aside while OBS is closed. If that session also leaks, record it under "OBS" in "Observations" and do not fail the item; otherwise fail the item. Finally move the files back.

### M11 Context menu, rename, and removal

Do and Pass, with `Main Output 1` recording. Rename and remove main outputs only through the dock's menus: obs-websocket `SetInputName` and `RemoveInput` bypass the code under test. To enter a name in the rename dialog, select the field's text with Ctrl+A and type the name with `xdotool type`; clear the field with Ctrl+A and Delete. Select a dock row by left-clicking its Filter name, and judge a selection with the pointer off the dock's table: under the pointer, a row shows a hover color that can match the selection color.

- The "+" button shows a plus icon, or the text "+" where the theme has none; note which. Its tooltip is "Add Branch Output", and its menu has "Main Output", "Sources", and "Scenes". "Sources" lists the sources placed in `Main` and `Other` (`Media`, `Name`, `Quad`, `Tone440`, and the color source of `Other`) in name order, ignoring case, and no main output; "Scenes" lists `Main` and `Other`.
- "+" → "Sources" → `Media` adds a filter `Branch Output` to `Media`, logs `Added Branch Output filter 'Branch Output' to 'Media'`, opens `Media`'s filters dialog with the filter in it, and adds dock rows with `Branch Output` in the Filter column and `Media` in the Source column. Choosing `Media` again adds `Branch Output 2`. "+" → "Scenes" → `Other` adds a filter whose rows show `Other` in the Source column. Close each filters dialog. The new filters' rows stay "Inactive" (no Apply yet).
- Right-clicking a filter's row opens a menu with only "Open Settings", which opens that source's filters dialog. Close it, then delete the three filters.
- Row hover and selection: turn Replay Buffer on through obs-websocket and wait for "Buffering"; `Main Output 1` now has a recording row and a replay buffer row. Hover with `xdotool mousemove X Y` without a click, and judge it on a screenshot taken with the pointer there. Take the reference colors from OBS's Sources list (an item under the pointer, and a selected item with the pointer elsewhere) and compare only the fill colors: the list can add a focus border that the band lacks. Controls with their own look (the checkboxes, the Status cell's buttons, and the Reset button, a reset icon centered in the last column) stay drawn over the band.
  - Hover over the recording row's Filter name, its Status cell, and its Reset button in turn: each time, that row shows one band in the hover color across every column from Filter to Reset, with no gap, border, or rounded corner at their boundaries, and the replay buffer row shows none. With the pointer below the last row, no row shows a band.
  - Select the recording row: only that row shows a band, in the selection color, and the replay buffer row is not selected. It stays selected after about 5 s (after the dock's refresh).
  - Press the Output column header, then press it again to reverse the order: the two rows swap places, and the selection follows the recording row. Press the Filter column header to return to the default order.
  - Select the replay buffer row: the selection moves to it. Left-click the empty area below the last row: no row is selected.
  - Left-clicking a row's own controls leaves the selection unchanged. Select the replay buffer row, then left-click the recording row's Reset button and its Source link (the dialog opens; close it with Cancel). Select the recording row, then turn the replay buffer row's checkbox off and on again: only the replay buffer stops and resumes. Do not left-click the eye icon here: it stops all of `Main Output 1`'s outputs.
- Right-clicking `Main Output 1`'s rows opens a menu with "Open Settings", "Rename", and "Remove", on every column of the recording row: the eye icon, the Source link, the Output link, the Status cell, and the Reset button. For each column, select the replay buffer row, right-click, check the menu, close it with Escape, and check that the selection has moved to the recording row alone. The right-click has no other effect (the eye icon keeps its state, and `<work>/xdg-open.log` gets no new line). "Open Settings" opens its properties dialog; close it with Cancel. Afterwards turn Replay Buffer off through obs-websocket.
- Rename: add a main output with "+" → "Main Output" and close its dialog with Cancel; it is `Main Output 2`. Choose "Rename" on its row: the dialog's field holds `Main Output 2`.
  - Enter `Main Output 1` and press OK: a warning says that another main output uses the name, the rename dialog reopens, and Cancel there leaves the name unchanged.
  - Clear the field and press OK: a warning asks for a name.
  - Enter `Renamed Output`, move the pointer off the dock with `xdotool mousemove X Y` without a click (for example onto OBS's preview), and press Return with `xdotool key Return` instead of clicking OK: its Filter cells show `Renamed Output`, its element in `branchOutputPrograms.json` has the name `Renamed Output`, and the log has `Renamed main output 'Main Output 2' to 'Renamed Output'`. Press the Filter column header to reverse the order, and move the pointer off the dock again: the two rows swap places, only `Renamed Output`'s row shows a band, in the selection color, and `Main Output 1`'s row shows none. The reversal moves a hover band left behind by the dialog onto `Main Output 1`'s row, apart from the selection. Press the Filter column header again to return to the default order. After an OBS restart it is still `Renamed Output`.
- Removal: turn Stream Recording on in `Renamed Output`, and wait for "Recording". Choose "Remove" on its row: the confirmation names `Renamed Output`, and No changes nothing. Choose "Remove" again and answer Yes: its rows leave the dock at once; within 5 s its recording stops and `Removed main output 'Renamed Output'` is logged; its file plays; `branchOutputPrograms.json` no longer lists it; and `Main Output 1` keeps recording to the same file.
- Removal while reconnecting: add a main output (`Main Output 2`), closing its dialog with Cancel, turn its Streaming on with one slot pointing at a local RTMP receiver, and wait for "Live". Stop the receiver and its restart loop, and once the row shows "Reconnecting", remove the main output with Yes: OBS stays responsive and does not crash, its rows leave the dock at once, and `Removed main output 'Main Output 2'` is logged within 30 s (the stop waits until 2 s after the last reconnect attempt, #214).
- Removal with the dialog open, in the dialog: add a main output (`Main Output 2`) and close its dialog with Cancel. Open the dialog again from its Source cell, set Resolution to "Half of canvas (50%)" without OK, and remove the main output from the dock with Yes: OBS's unsaved-changes prompt appears. Choose "Discard": the dialog closes, and `Removed main output 'Main Output 2'` is logged.
- Undo, in the dialog: add a main output (`Main Output 2`), set Resolution to "Half of canvas (50%)" in its dialog, and press OK. Set Program to `Other` through obs-websocket, remove the main output with Yes, and choose Edit → Undo: Program returns to `Main`, OBS does not crash, and the main output returns neither to the dock nor to `branchOutputPrograms.json`. Between the OK and the Undo, do no other GUI operation that OBS can undo (for example adding or deleting a filter). Do not judge by `Null 'source' parameter`: libobs logs it only with `--verbose`.
- Quit OBS: the log ends with `Number of memory leaks: 0` (allowed leaks as in R19). Launch it again: the dock and `branchOutputPrograms.json` have only `Main Output 1`.

Afterwards make sure that `Main` is in Program and that `Main Output 1` is the only main output, removing any other that a failed step left.

### M12 Hotkeys

A main output's hotkeys are OBS frontend hotkeys: Settings → Hotkeys lists them at the top among OBS's own hotkeys, under no source or scene, and each description names the main output (for example `Enable 'Main Output 1'`, `Split 'Main Output 1' Recording File`). OBS saves an assignment made there in `[Hotkeys]` of the profile's `basic.ini`, and the plugin keeps a copy in `hotkey_bindings` of the main output's `settings` in `branchOutputPrograms.json`. obs-websocket `GetHotkeyList` lists their names, which end in `.{uuid}` with the main output's `uuid`. Judge a hotkey list in Settings → Hotkeys opened after the operation.

Do: with `Main Output 1` recording, set its Automatic File Splitting to "Only split manually" and its recording format to Hybrid MP4. Add `Main Output 2` with "+" → "Main Output", closing its dialog with Cancel, and through obs-websocket turn its Replay Buffer on and its Streaming and Stream Recording off.

Do and Pass:

- Registration by output type: Settings → Hotkeys lists, for `Main Output 1`, the Enable / Disable pair and the 6 recording items (Split, Pause, Unpause, Add chapter, Enable, Disable), and none for streaming or the replay buffer; for `Main Output 2`, the Enable / Disable pair and the 3 replay buffer items (Save, Enable, Disable). Then turn `Main Output 1`'s Streaming on with Stream Count 1, its slot pointing at a local RTMP receiver, and its Replay Buffer on, and wait for "Live" and "Buffering": `Main Output 1` now has 15 items, adding the all-streaming pair, the `Streaming 1` pair (no `Streaming 2`), and the 3 replay buffer items.
- Key actions: assign unused combinations, none of them R13's, to the 15 items of `Main Output 1` and to `Save 'Main Output 2' Replay Buffer`. Press each of `Main Output 1`'s as R13 does, with its streaming off while testing Pause and Unpause; R13's notes on Split and Add chapter apply. Each key acts on `Main Output 1` only, and the dock checkboxes update immediately: `Main Output 2` stays enabled and "Buffering", and saves no replay buffer file. `Main Output 2`'s Save key saves only its own replay buffer. Finish with `Main Output 1`'s streaming turned off by its Disable All Streaming key, and stop the receiver.
- Persistence: after each operation below, Settings → Hotkeys shows the 16 assignments.
  - Turn `Main Output 1`'s Stream Recording off through obs-websocket: its 6 recording items leave the list. Turn it on again. Do the same with Replay Buffer (3 items) and Streaming (the all-streaming and `Streaming 1` pairs; its streaming stays off from the key).
  - Rename `Main Output 2` to `Hotkey Output` with "Rename" in its row's menu, entering the name as M11 does: its 5 items name `Hotkey Output` (for example `Save 'Hotkey Output' Replay Buffer`).
  - Restart OBS.
  - Launch with `--safe-mode`, exit, and launch normally.
  - Switch to the profile `BORegression2` and back. While in `BORegression2`, no item naming a main output has a key.
  - Note the names that `GetHotkeyList` lists for `Main Output 1`, and quit OBS: `[Hotkeys]` in `BORegression`'s `basic.ini` has an entry with a non-empty `bindings` for each of them. Delete `hotkey_bindings` from `Main Output 1`'s `settings` in `BORegression`'s `branchOutputPrograms.json`, and launch OBS: the assignments come back from `[Hotkeys]`. Restart the receiver and press `Main Output 1`'s Enable All Streaming key: its slot goes "Live".
- Removal, in the dialog: note `Hotkey Output`'s `uuid` in `branchOutputPrograms.json`. Open its dialog from its Source cell, set Resolution to "Half of canvas (50%)" without OK, and remove `Hotkey Output` from the dock with Yes as M11 does: OBS's unsaved-changes prompt appears. Keep it open until `Removed main output 'Hotkey Output'` is logged; `GetHotkeyList` then lists no name ending in its `uuid`. Choose "Save": `GetHotkeyList` still lists none of its names, and Settings → Hotkeys has none of its items, while `Main Output 1`'s 15 keep their keys. Do not judge by `Hotkey Output: Main output updated`: whether Save logs it depends on the OBS version and timing.

Afterwards turn `Main Output 1`'s Streaming, Replay Buffer, and Automatic File Splitting off and its recording format back to its value before M12 through obs-websocket, stop the receiver, and make sure that `Main Output 1` is the only main output.
