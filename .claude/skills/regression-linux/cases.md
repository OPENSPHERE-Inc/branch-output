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

Do: delete `recently.json` from `<config>/plugin_config/osi-branch-output/` if it exists, set the profile's streaming encoder to the hardware encoder, add a Branch Output filter to `Media`, turn Stream Recording on, and Apply. Afterwards set the profile encoder back to x264, and switch the filter to x264 and Apply (a new filter copies the last applied filter's settings from `recently.json`).

Pass:

- The Docks menu has "Branch Output Status", and it lists the new filter's rows.
- The new filter's Video Encoder is the profile's hardware encoder, and the recording it writes plays. With no hardware encoder offered, use x264 and note it.

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
- ffprobe: 1280x720 video, one audio stream carrying the 1 kHz tone, duration within 1 s of the time between the recording's start and stop log lines.
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

Do: on two filters, turn Replay Buffer on with Maximum Replay Time 10 s and "Show estimated memory usage" on, and set the video encoder's keyframe interval to 1 s. Wait more than 10 s, save with one row's Save button, then with "Save All Replay Buffers".

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
