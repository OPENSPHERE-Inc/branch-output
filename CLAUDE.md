# CLAUDE.md — Branch Output Plugin for OBS Studio

## Project Overview

**Branch Output** is an OBS Studio plugin (shared library / module) that adds an effect filter allowing
individual sources or scenes to stream and/or record independently from the main OBS output.
It also adds **Program Outputs**, which stream and/or record the OBS program (the main canvas)
with their own encoders and settings, in the same way as the filter.
It is developed and maintained by **OPENSPHERE Inc.** under the GPLv2+ license.

- Repository language: **C++ 17** with **Qt 6** for UI
- Build system: **CMake** (≥ 3.16)
- Based on the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
- Target OBS Studio version: **≥ 30.1.0** (Qt6, x64 / ARM64 / Apple Silicon)

---

## Repository Layout

```
branch-output/
├── CMakeLists.txt          # Top-level CMake project definition
├── CMakePresets.json        # CMake presets (windows-x64, macos, linux-x86_64, CI variants)
├── buildspec.json           # Build specification (name, version, dependencies, UUIDs)
├── build.ps1                # Local Windows build script (RelWithDebInfo)
├── .clang-format            # C++ code style (clang-format ≥ 16)
├── .cmake-format.json       # CMake code style
├── API.md / API_ja.md       # Script API reference (proc handlers, sample scripts)
├── .github/
│   ├── actions/             # Reusable format-check actions
│   └── workflows/           # CI workflows (build, format check, release)
├── cmake/                   # CMake helpers & platform-specific modules
├── build-aux/               # Auxiliary build scripts (format runners)
├── data/
│   ├── locale/              # Translation files (en-US, ja-JP, zh-CN, ko-KR, etc.)
│   └── scripts/             # Sample Lua / Python scripts (recording / replay buffer file names)
├── src/
│   ├── branch-output.cpp / .hpp  # BranchOutput base class (shared output logic, virtual hooks)
│   ├── plugin-main.cpp      # Module entry points, script list procs, BranchOutputFilter
│   ├── plugin-main.hpp      # BranchOutputFilter class declaration
│   ├── plugin-program.cpp / .hpp  # BranchOutputProgram (Program Output) and its source type
│   ├── program-manager.cpp / .hpp  # BranchOutputProgramManager (owns / persists Program Outputs)
│   ├── plugin-hotkey.cpp    # Hotkey registration sync, binding cache, filter enable/disable callbacks
│   ├── plugin-streaming.cpp # Streaming output logic (individual start/stop, per-slot control)
│   ├── plugin-stream-recording.cpp  # Stream recording output logic (individual start/stop)
│   ├── plugin-replay-buffer.cpp  # Replay buffer output logic (individual start/stop)
│   ├── plugin-ui.cpp        # Shared property groups, defaults, filter properties, dock creation
│   ├── plugin-support.h     # Auto-generated plugin support header
│   ├── plugin-support.c.in  # Template for plugin-support.c
│   ├── utils.cpp / .hpp     # Utility functions, encoder helpers, profile helpers
│   ├── audio/
│   │   └── audio-capture.cpp / .hpp  # Audio capture abstraction
│   ├── video/
│   │   ├── filter-video-capture.cpp / .hpp  # Filter input video capture (GPU texrender proxy)
│   │   ├── main-texture-proxy.cpp / .hpp  # Program Output proxy path (main texture proxy source)
│   │   └── crop-rect-preview-renderer.cpp / .hpp  # Crop rectangle preview overlay renderer
│   └── UI/
│       ├── output-status-dock.cpp / .hpp  # Status dock widget
│       ├── resources.qrc    # Qt resource file
│       └── images/          # Icons and images
└── release/                 # Build output directory
```

---

## Build Instructions

### Prerequisites

| Platform | Toolchain |
|----------|-----------|
| Windows  | Visual Studio 17 2022, CMake ≥ 3.16, Qt 6 |
| macOS    | Xcode ≤ 16.4.0 (macOS SDK ≤ 15.5), CMake ≥ 3.16, Qt 6 |
| Linux    | GCC / Clang, CMake ≥ 3.16, Qt 6 |

> **Note (macOS):** This project requires **Xcode 16.4.0 (macOS 15.5 SDK) or below**. Xcode 26 / macOS SDK 26 and later are currently unsupported due to compatibility constraints with OBS Studio's build dependencies.

OBS Studio sources and pre-built dependencies are fetched automatically via `buildspec.json`
(obs-studio 30.1.2, obs-deps, Qt6).

### clang-format

This project requires **clang-format version 16 or later** (as specified in `.clang-format`).
CI (GitHub Actions) enforces version 17.0.3 exactly via `build-aux/.run-format.zsh`.

| Platform | Install method |
|----------|----------------|
| Windows  | `winget install -v 17.0.3 LLVM.LLVM` or download from the [LLVM 17.0.3 release page](https://github.com/llvm/llvm-project/releases/tag/llvmorg-17.0.3). Ensure `clang-format` is on your `PATH`. |
| macOS    | `brew install llvm@17` — then use `$(brew --prefix llvm@17)/bin/clang-format` or add it to `PATH`. |
| Linux    | Install via package manager (e.g., `apt install clang-format-17` on Ubuntu/Debian, or `dnf install clang-tools-extra-17.0.3` on Fedora). The script also looks for `clang-format-17` on `PATH`. |

Verify installation:

```bash
clang-format --version
# Expected: clang-format version 17.0.3 (...)
```

To format all source files locally:

```powershell
# PowerShell (Windows / cross-platform)
Get-ChildItem -Recurse -Path src -Include *.cpp,*.hpp,*.h,*.c | ForEach-Object { clang-format -i -style=file -fallback-style=none $_.FullName }
```

```bash
# Bash / Zsh (macOS / Linux)
find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.c' \) -exec clang-format -i -style=file -fallback-style=none {} +
```

### cmake-format

This project requires **cmake-format version 0.6.13 or later**. Configuration is in `.cmake-format.json`.
CI (GitHub Actions) enforces this version via `build-aux/.run-format.zsh`.

Install via pip:

```bash
pip install cmake-format>=0.6.13
```

Verify installation:

```bash
cmake-format --version
# Expected: 0.6.13 or later
```

To format all CMake files locally:

```powershell
# PowerShell (Windows / cross-platform)
Get-ChildItem -Recurse -Include CMakeLists.txt,*.cmake | Where-Object { $_.FullName -notmatch 'build_' } | ForEach-Object { cmake-format -i $_.FullName }
```

```bash
# Bash / Zsh (macOS / Linux)
find . -type f \( -name 'CMakeLists.txt' -o -name '*.cmake' \) -not -path '*/build_*/*' -exec cmake-format -i {} +
```

### Windows (local)

```powershell
# Configure + Build + Install
.\build.ps1

# With Inno Setup installer
.\build.ps1 -installer
```

Or manually:

```powershell
cmake --fresh -S . -B build_x64 -G "Visual Studio 17 2022" -A x64
cmake --build build_x64 --config RelWithDebInfo
cmake --install build_x64 --prefix release/Package --config RelWithDebInfo
```

### CMake Presets

Use `cmake --preset <name>` with one of:
- `windows-x64` / `windows-ci-x64`
- `macos` / `macos-ci`
- `linux-x86_64` / `linux-ci-x86_64`

---

## Architecture & Key Concepts

### Plugin Entry Point

`obs_module_load()` (`plugin-main.cpp`) registers four source types via `obs_source_info`:

- `osi_branch_output` (`FILTER_ID`): the effect filter (`BranchOutputFilter::createFilterInfo()`).
- `osi_branch_output_proxy` (`PROXY_SOURCE_ID`): the private proxy source of filter input mode
  (`FilterVideoCapture::createProxySourceInfo()`).
- `osi_branch_output_program` (`PROGRAM_SOURCE_ID`): the private context source of a Program Output
  (`BranchOutputProgram::createProgramInfo()`).
- `osi_branch_output_program_proxy` (`MAIN_TEXTURE_PROXY_SOURCE_ID`): the private proxy source of
  the Program Output proxy path (`createMainTextureProxySourceInfo()`).

`obs_module_post_load()` publishes an empty output-list snapshot, installs the status table
accessibility factory, creates the status dock, creates the `BranchOutputProgramManager`, and
registers the global procs (see Modifying the Script API), in this order.

`obs_module_unload()` sets `moduleUnloading`, deletes the manager, releases the output-list
snapshot, removes the dock, and removes the accessibility factory.

### Class Structure

- `BranchOutput` (`branch-output.hpp`, a `QObject` subclass) holds the state and logic shared by all
  outputs.
  - It covers the applied settings snapshot, the output infrastructure (`ensureInfrastructure()` /
    `releaseInfrastructureIfIdle()`), output start / stop, the interval timer handler
    (`onIntervalTimerTimeout()`), hotkey synchronization, and the property groups.
  - It is implemented in `branch-output.cpp`, the per-output-type files (`plugin-streaming.cpp` /
    `plugin-stream-recording.cpp` / `plugin-replay-buffer.cpp`), `plugin-hotkey.cpp`, and
    `plugin-ui.cpp`.
  - The parts that depend on the input are pure virtual hooks implemented by the derived classes:
    the input (`validateInput()`, `isInputAvailable()`, `getInputName()`, etc.), the video input
    (`selectVideoInputMode()`, `setupVideoInput()`, `teardownVideoInput()`,
    `isVideoInputOutdated()`), the default audio (`setupDefaultAudio()`), blanking
    (`evaluateBlanking()`, `getBlankingState()`), `hasFilterPipeline()`, hotkey registration
    (`canRegisterHotkeys()`, `beginHotkeyRegistration()`, `endHotkeyRegistration()`,
    `registerHotkey()`, `registerHotkeyPair()`), `openSettings()`, and `updateCallback()`.
  - The constructor of each derived class calls `initializeSettings()`, which migrates the settings,
    applies `pinOutputLineup()`, publishes the applied snapshot, and registers the per-output procs
    without calling any virtual hook.
- `BranchOutputFilter` (`plugin-main.hpp`) takes the filter's parent source as its input.
  - It is implemented in `plugin-main.cpp`, with its hotkey registration hooks in
    `plugin-hotkey.cpp` and its `getProperties()` in `plugin-ui.cpp`.
- `BranchOutputProgram` (`plugin-program.hpp` / `.cpp`) takes the OBS program as its input.
- `BranchOutputProgramManager` (`program-manager.hpp` / `.cpp`) is the only owner of Program Outputs
  and runs on the UI thread only.
- `hasFilterPipeline()` is true for the filter only, and switches the filter-specific options and
  behavior: the video source type (filter input), blanking and muting, filter audio, and pausing the
  recording while the source is collapsed.
  - When it is false (Program Output), the resolution options are labeled with the
    `Resolution.Program.*` locale keys.

### Core Responsibilities of BranchOutput

Rows without a note apply to both the filter and Program Outputs.

| Area | Description |
|------|-------------|
| **Streaming** | Creates up to `MAX_SERVICES` (8) independent streaming outputs, each with its own service and reconnect logic. All streaming slots, the recording, and the replay buffer share one video encoder per instance and one audio encoder per track. |
| **Recording** | Supports file recording with various container formats, time/size-based splitting, pause/unpause, and chapter markers. |
| **Replay Buffer** | Supports replay buffer output (implemented in `plugin-replay-buffer.cpp`). Allows saving the last N seconds of encoded output to file on demand via hotkey or UI button. |
| **Audio** | Takes up to `MAX_AUDIO_MIXES` tracks from `AudioCapture` (silence) or its subclasses, or from OBS's mixes. Without custom audio, the filter uses the filter audio and a Program Output uses master track 1. |
| **Video** | Each output builds its video input in `setupVideoInput()`, called from `ensureInfrastructure()`. The filter uses an OBS view of its parent source (or of a crop scene or the filter input proxy), and a Program Output uses OBS's main video (direct path) or a view of the main texture proxy (proxy path). |
| **UI** | Properties are built from the shared groups in `plugin-ui.cpp`, and the filter-specific options appear only when `hasFilterPipeline()` is true. The status dock (`BranchOutputStatusDock`) lists every output of the filters and Program Outputs, including replay buffer save buttons. |
| **Hotkeys** | Registers hotkeys (pairs and single hotkeys) for enable/disable, split recording, pause/unpause, chapter markers, save replay buffer, and per-output enable/disable (streaming all slots and per slot, recording, replay buffer). `syncHotkeys()` registers/unregisters only the difference between the desired set (derived from settings) and the currently registered set. Bindings are captured into `hotkeyBindingsCache` before unregistering and persisted to the `hotkey_bindings` settings key; on registration they are restored from the cache when libobs restored nothing. Renaming updates descriptions only. The filter registers them on its parent source, and a Program Output registers them as frontend hotkeys and also restores their assignments from the profile's `[Hotkeys]`. |
| **Interlock** | Can link output activation to OBS streaming, recording, virtual camera, replay buffer, or individual (per-output-type) states. The "Individual" mode maps each Branch Output type to its OBS counterpart independently. |
| **Individual Start/Stop** | Allows streaming, recording, and replay buffer to be started/stopped independently via `ensureInfrastructure()` / `releaseInfrastructureIfIdle()` to separate shared resource lifecycle from individual output lifecycle. Per-output user intent is tracked via atomic flags (`streamingUserEnabled[]`, `recordingUserEnabled`, `replayBufferUserEnabled`). |
| **Cropping** | Supports relative (margin) and absolute (region) video cropping with `CropRect` struct. Live preview via `CropRectPreviewRenderer`. |
| **Blanking** | Filter only: when `blank_when_not_visible` is set and the parent source is not visible in Program (`sourceVisibleInProgram()`), the filter clears the view's source (`obs_view_set_source(view, 0, nullptr)`) to output black, and also stops the audio captures (`setAudioCapturesActive(false)`) when `mute_audio_when_blank` is set. Program Outputs have no blanking (`evaluateBlanking()` always returns false). |
| **Program Output** | `BranchOutputProgram` outputs the OBS program through a private context source (`osi_branch_output_program`). `BranchOutputProgramManager` owns the Program Outputs on the UI thread and saves them to `branchOutputPrograms.json` in the profile. |

### Threading Model

- OBS callbacks (video render, audio filter, video tick) may run on **different threads** from the UI thread.
- Recursive mutexes (`pthread_mutex_init_recursive()`): `pluginMutex` (module scope) and
  `outputMutex` / `audioMutex` / `AppliedSettings::mutex` (per output instance, i.e. per
  `BranchOutput`).
  - Lock ordering: `pluginMutex` → `outputMutex` → `audioMutex` → `AppliedSettings::mutex` (leaf:
    nothing is acquired while it is held).
- `outputListSnapshotMutex` (module scope, `PTHREAD_MUTEX_INITIALIZER`, non-recursive, never
  destroyed) guards only the output-list snapshot swap and is never nested with the others.
- `audioMutex` protects audio capture pointers against concurrent release in `releaseInfrastructureIfIdle()`.
- `QMutex` protects audio buffers in `AudioCapture`.
- Atomic fields (`std::atomic<bool>` for `outputStarting`, `streamingUserEnabled[]`, `recordingUserEnabled`, `replayBufferUserEnabled`; `std::atomic<uint64_t>` for `reconnectAttemptingAt`, `startCount`, `recordingStartCount`, `recordingBytesStartCount`, `replayBufferStartCount`) eliminate data races from OBS signal callbacks.
- UI updates use `QMetaObject::invokeMethod` with `Qt::QueuedConnection` for thread safety.
- UI thread only: `BranchOutputProgramManager`, `attach()` / `detach()` / `setSuspended()` /
  `beginRemoval()` / `openSettings()` of `BranchOutputProgram`, and the status dock.
  - Program Output hotkey registration reads the profile config, so it runs on the UI thread only:
    `BranchOutputProgram::updateCallback()`, which runs on the graphics thread for a deferred
    update, queues `syncHotkeys()` to the UI thread.
- `AppliedSettings` (`appliedSettings`) holds a copy of the settings as last applied, guarded by its
  own leaf mutex (taken last).
  - `updateCallback()` publishes a new copy and does not restart the output; a Program Output
    publishes one only when the settings changed (see Applied settings snapshot under Important
    Warnings).
  - The interval timer restarts the output when the copy it reads is not the one the running
    infrastructure was built from (`activeSettings`).

### OBS API Usage

The plugin heavily uses:
- `obs-module.h` — Module registration, locale, config paths
- `obs-frontend-api.h` — Profile config, scene enumeration, frontend events
- `obs.hpp` — OBS RAII wrappers (`OBSSourceAutoRelease`, `OBSEncoderAutoRelease`, `OBSOutputAutoRelease`, etc.)
- `util/deque.h`, `util/threading.h`, `util/platform.h` — Low-level utilities

---

## Code Style & Formatting

### C++ (clang-format)

- **Standard**: C++17
- **Column limit**: 120
- **Indent**: 4 spaces (tabs not used)
- **Brace style**: Custom — functions use next-line braces; control statements use same-line braces
- **clang-format version**: ≥ 16 required
- Run: `clang-format -i src/**/*.cpp src/**/*.hpp`

### CMake (cmake-format)

- **Line width**: 120
- **Tab size**: 2
- Config: `.cmake-format.json`

### CI Enforcement

Format is checked in CI via `.github/workflows/check-format.yaml` using reusable actions
(`run-clang-format`, `run-cmake-format`). PRs to `master`/`main`/`dev` and pushes to `master`/`dev` are validated.

---

## Coding Guidelines

### General Rules

1. **Follow OBS plugin conventions** — Use `obs_log()` for logging, `OBS_DECLARE_MODULE()` for entry, locale via `obs_module_text()`.
2. **RAII wrappers** — Always use `OBSSourceAutoRelease`, `OBSDataAutoRelease`, etc. instead of manual `obs_*_release()`.
3. **Thread safety** — Any data shared between OBS callbacks and UI must be guarded by mutex. Use `QMetaObject::invokeMethod` with `Qt::QueuedConnection` when calling UI methods from non-UI threads.
4. **Settings migration** — When changing settings schema, add backward-compatible migration code to
   `BranchOutput::initializeSettings()`, which the constructors of both derived classes call (see
   the `audio_source` migration example there).
5. **Qt MOC** — Classes using `Q_OBJECT` must be in headers. `AUTOMOC`, `AUTOUIC`, `AUTORCC` are enabled.
6. **Callback data** — Hand a `BranchOutput` instance to OBS callbacks only through
   `toCallbackData()`, and convert the data pointer back only through
   `BranchOutput::fromCallbackData()`.
   - In callbacks a derived class registered, use `BranchOutputFilter::fromFilterCallbackData()` /
     `BranchOutputProgram::fromProgramCallbackData()`.
   - Do not `static_cast` a `void *` that points to a `BranchOutput` instance.
   - This applies only to data that carries a `BranchOutput` instance: other data (temporary structs
     such as `SyncContext` in `plugin-hotkey.cpp`, the frontend event callbacks of the dock and the
     manager, `MainTextureProxyContext`) is cast with `static_cast`.

### Naming Conventions

- **Classes**: PascalCase (`BranchOutput`, `BranchOutputFilter`, `BranchOutputProgram`,
  `AudioCapture`, `FilterVideoCapture`, `CropRectPreviewRenderer`, `OutputCell`, `OutputTableRow`)
- **Methods**: camelCase (`startOutput`, `stopRecordingOutput`, `onIntervalTimerTimeout`)
- **Constants/Macros**: UPPER_SNAKE_CASE (`MAX_SERVICES`, `FILTER_ID`, `OUTPUT_MAX_RETRIES`)
- **Member variables**: camelCase, no prefix (`contextSource`, `videoEncoder`, `recordingActive`)
- **Static callbacks**: camelCase with descriptive prefix (`onEnableFilterHotkeyPressed`, `audioFilterCallback`)

### Ternary Operators

- **Avoid multi-line ternary expressions** — clang-format cannot reliably format nested or multi-line ternary operators (`a ? b : c`) in a readable way. Use `if`/`else if`/`else` statements instead when the expression would span multiple lines.

### Error Handling

- Use `obs_log(LOG_ERROR, ...)` for errors, `LOG_WARNING` for warnings, `LOG_INFO` for lifecycle events, `LOG_DEBUG` for verbose tracing.
- Check return values from OBS API calls; handle gracefully (do not crash OBS).
- Never throw C++ exceptions across OBS API boundaries.

### Localization

- All user-facing strings must use `obs_module_text("Key")` or the `QTStr("Key")` helper.
- Add new keys to `data/locale/en-US.ini` (primary) and ideally to `ja-JP.ini`.
- Locale files use INI format: `Key="Value"`.

---

## Testing & Validation

- There are no automated unit tests in this repository currently.
- **Manual testing** is required: load the plugin in OBS Studio, add the "Branch Output" filter and
  a Program Output (status dock "+" → Program), and verify streaming/recording behavior.
- Test on all supported platforms when possible (Windows x64, macOS, Linux), on OBS 30.1.x and on
  the latest stable release.
- Verify Studio Mode compatibility: Branch Output filters ignore studio mode's program and output
  from preview, while a Program Output outputs the Program side, not the Preview.
- Verify that enabling and then disabling the Branch Output filter does not cause a crash.
- Verify that shutting down OBS does not crash when the Branch Output filter is inactive or active, respectively.
- Verify that switching scene collections does not crash when the Branch Output filter is inactive or active, respectively.
- Verify that no memory leaks are logged on OBS shutdown.
- Verify that mutex does not cause deadlocks.
- Verify that blanking mode does not leak video or audio unintentionally — when the source is not visible in the main output (Program) and blanking is enabled, the Branch Output must emit a black frame (no source imagery). If audio muting is also enabled, all audio tracks (including master track) must be silenced.
- Verify that Individual interlock mode correctly starts/stops each output type independently following its OBS counterpart.
- Verify that per-output checkboxes in the Status Dock correctly enable/disable individual outputs in all interlock modes.
- Verify that per-output hotkeys toggle the correct output and that the Status Dock checkboxes sync immediately.
- Verify that video cropping (both relative and absolute modes) produces the correct output resolution and content.
- Verify that the crop preview rectangle displays correctly and is hidden when the properties dialog closes.
- Verify that turning an output type off, applying, and turning it back on keeps its hotkey assignments, and that they survive an OBS restart.
- Verify that renaming the filter keeps its hotkey assignments and renders the new name in the hotkey descriptions.
- Verify that deleting the filter and undoing the deletion restores its hotkey assignments.
- Verify that hotkey assignments are restored in normal mode after OBS has been started and shut down in safe mode.
- Verify on OBS 30.1–32.x that, while the replay buffer is running, Settings → Hotkeys shows no
  "Save Replay" entry under the filter or Program Output name, and that the output's own
  `Save '<name>' Replay Buffer` hotkey saves a replay.
- Verify that adding a Program Output ("+" → Program), configuring it, renaming and removing it from
  the row menu, and toggling its enable checkbox do not crash, and that Program Outputs are restored
  after an OBS restart and after switching profiles away and back.
- Verify that switching scene collections or profiles, or shutting down OBS, while a Program Output
  is streaming, recording, or running the replay buffer does not crash and logs no leaks.
- Verify that a Program Output outputs the Program side in Studio Mode, and that transitions appear
  in its output.
- Verify that a Program Output's cropping and resolution settings give the expected output, and that
  the properties dialog preview matches it.
- Verify that a Program Output chooses the direct path and the proxy path as expected
  (`Video path: direct` / `Video path: proxy` in the log), and that OBS's own streaming and
  recording are unaffected.
- Verify that a Program Output's custom audio sources and multitrack audio work, including when a
  custom audio source disappears after a scene collection switch.
- Verify that Program Output hotkeys can be assigned, survive an OBS restart and a profile round
  trip, show the new name after a rename, and are no longer registered after the Program Output is
  removed.
- Verify that `osi_branch_output_get_filter_list` lists no Program Output, that
  `osi_branch_output_get_output_list` lists filters and Program Outputs, and that the bundled
  scripts work with both.

---

## CI / CD

| Workflow | Trigger | Purpose |
|----------|---------|---------|
| `push.yaml` | Push to `master`/`main`/`dev`/`release/**`, tags | Format check + build + release creation |
| `build-project.yaml` | Called by push/PR workflows | Multi-platform build (Windows, macOS, Linux) |
| `check-format.yaml` | Called by push/PR workflows | clang-format and cmake-format validation |
| `pr-pull.yaml` | Pull requests to `master`/`main`/`dev` | Format check + build validation |
| `dispatch.yaml` | Manual dispatch | On-demand builds |

Release tags follow semver: `X.Y.Z` for stable, `X.Y.Z-beta`/`X.Y.Z-rc` for pre-releases.

---

## Common Tasks for AI Agents

### Adding a New Setting / Feature

1. Add the setting key to `BranchOutput::getDefaults()` in `plugin-ui.cpp`.
   - A Program Output takes its defaults from `BranchOutputProgram::getProgramDefaults()`
     (`plugin-program.cpp`), which calls it and then changes the `resolution` default to `output`.
2. Add UI controls to the shared groups in `plugin-ui.cpp` (use OBS properties API:
   `obs_properties_add_*`), which appear in both properties dialogs.
   - Put filter-only settings inside a `hasFilterPipeline()` check.
3. Handle the setting in `ensureInfrastructure()` / `startOutput()` / `stopOutput()` and the
   per-output-type helpers as needed.
   - `updateCallback()` publishes the snapshot and does not restart outputs (see Applied settings
     snapshot under Important Warnings).
   - Add a key written outside the properties dialog (such as a flag written on save) to
     `nonPropertyKeys` in `plugin-program.cpp`; otherwise a Program Output takes that write for a
     settings change and restarts.
   - Add a key that decides which outputs run or where they stream to
     `BranchOutput::pinOutputLineup()` (Discard in the properties dialog restores only the keys that
     had a user value when it opened).
4. Add locale strings to **all** locale files under `data/locale/` (`en-US.ini`, `ja-JP.ini`, `zh-CN.ini`, `ko-KR.ini`, `de-DE.ini`, `fr-FR.ini`, `ca-ES.ini`, `ro-RO.ini`, `ru-RU.ini`, `uk-UA.ini`). `en-US.ini` is the primary (required); all others should also be updated.
5. Ensure backward compatibility — existing saved settings must still load correctly.
   - Put migration code in `BranchOutput::initializeSettings()`.

### Adding a New Streaming Service Slot

- The plugin supports up to `MAX_SERVICES` (8) service slots.
- Each slot has its own `BranchOutputStreamingContext` with output, service, and signals.
- Use `getIndexedPropNameFormat()` for indexed property names.

### Modifying Audio Handling

- Audio capture is abstracted through `AudioCapture` class in `src/audio/`.
- The class manages an audio buffer, supports push/pop patterns, and handles format conversion.
- Audio tracks and the audio encoders are set up in `BranchOutput::ensureInfrastructure()`.
- A track takes its audio from `AudioCapture` itself (silence, `AudioCapture::silenceCapture`), a
  subclass (`SourceAudioCapture`, `FilterAudioCapture`, `MasterAudioCapture`), or an OBS mix
  (`obs_get_audio()` with the track's mix index).
- Without custom audio, the derived class's `setupDefaultAudio()` sets up track 1:
  - Filter: the filter audio (`FilterAudioCapture`).
  - Program Output: master track 1 (`obs_get_audio()`).
- With custom audio, each track is disabled, silence, a master track, the filter audio, or a
  specific source's audio (`SourceAudioCapture`).
  - A master track uses `MasterAudioCapture` (so that it can be muted) only on a filter with both
    blanking and muting enabled, and `obs_get_audio()` otherwise.
  - Filter audio is available on the filter only; a Program Output logs a warning and skips that
    track.

### Modifying the Status Dock

- `BranchOutputStatusDock` in `src/UI/` is a `QFrame`-based dock widget.
- Its table is `OutputStatusTable` (a `QTableWidget` subclass) with custom cell classes:
  `OutputTableCellItem`, `LabelCell`, `FilterCell` (enable checkbox and output name), `ParentCell`
  (input name link), `OutputCell`, and `StatusCell`.
  - Each row is an `OutputTableRow`; the dock and the rows are friends of `BranchOutput` and read
    its protected members.
- `OutputCell` provides per-output enable/disable checkboxes (eye icon) and folder-open click for recording/replay buffer rows.
- The `outputUserEnabledChanged` signal provides immediate checkbox sync when state changes from hotkeys or other sources.
- Thread-safe updates via `QMetaObject::invokeMethod`.
- Reset records the Drop Frames / Sent Size baselines together with the output's start counts, and `OutputTableRow::update()` clears a baseline when its start count has changed. Streaming slots increment `startCount` in the "activate" handler; recording and replay buffer increment `recordingStartCount` / `replayBufferStartCount` right after a successful `obs_output_start()`, in the same `outputMutex` critical section. The recording's Sent Size baseline follows `recordingBytesStartCount` instead, which only a start that restarts the byte count from 0 increments (`mp4_output` / `mov_output` keep it across a stop / start of the same object). Any new path that starts a recording or replay buffer output must increment, the same way, each count whose counters that start restarts from 0.
- `addOutput(BranchOutput *)` creates the rows of an output, and `removeOutput()` removes them.
  - An output gets one row for the recording, one for the replay buffer, and one per enabled
    streaming slot, or a single "None" row when none of them is enabled.
- The input name link calls `openSettings()`: the parent source's filters dialog for a filter, and
  the properties dialog for a Program Output.
  - The Source column of a Program Output row shows "Program" (`getInputName()`).
- The "+" button (`showAddMenu()`):
  - "Program" emits `addProgramRequested`, and `BranchOutputProgramManager::addProgram()` creates a
    private source with the next default name, owns and saves it, and opens its properties dialog.
  - The "Sources" / "Scenes" submenus call `addBranchOutputFilter()`, which adds a filter named the
    same way as in OBS's filters dialog and opens that source's filters dialog.
- The row context menu (`onTableContextMenuRequested()`) has "Open Settings" on every row, and
  "Rename" and "Remove" on Program Output rows only.
  - The menu and the dialogs opened from it run nested event loops, during which the dock timer can
    delete the row, so the handler copies the weak reference and the name before opening the menu
    and touches no row afterwards.
  - Rename rejects an empty name and a name another Program Output uses, with a warning;
    `BranchOutputProgramManager::renameProgram()` applies the same checks.
  - Remove asks for confirmation (default: No) and then emits `removeProgramRequested`, which calls
    `BranchOutputProgramManager::removeProgram()`.
- Removal order (`removeProgram()` and `finishRemoval()`):
  1. Move the source from `programs` to `removingPrograms`, and connect `removalReady` first,
     because `beginRemoval()` emits it at once when no output is running.
  2. `beginRemoval()` sets `removing` (which stops output starts and row re-adds), removes the dock
     rows, and starts a graceful stop.
  3. `savePrograms()`.
  4. `obs_source_remove()`, last, because closing an open properties dialog can run a nested event
     loop (unsaved-changes prompt).
  5. When the graceful stop has finished and `removalReady` arrives, `finishRemoval()` calls
     `detach()` (synchronous `stopOutput()` and hotkey unregistration) and drops the last strong
     reference.
- The row band (`OutputTableRowDelegate`) draws one band across the whole hovered row and the
  clicked row.
  - Themes define the hover and selected colors only for list items, so the band is drawn as
    `PE_PanelItemViewItem` with a hidden `QListWidget` as the style reference.
  - The table selection is unused (`NoSelection`), and the grid is turned off so that the band shows
    no gaps between cells.
  - The viewport event filter (`eventFilter()`) updates the hovered and selected rows.
  - Qt sends no HoverLeave while a context menu is open, so the band is dropped during the menu and
    derived again from the pointer position afterwards (`resyncHoveredRow()`), except on Wayland,
    where it stays dropped.
- `addOutput()` / `removeOutput()` and row renames publish the output-list snapshot through
  `publishOutputListSnapshot()`.
  - The snapshot is built only from the rows' own copies (`BranchOutputInfo`) and never reads
    `row->branchOutput`, which may already be freed.

### Modifying Replay Buffer

- Replay buffer logic is implemented in `src/plugin-replay-buffer.cpp`.
- `BranchOutput` manages replay buffer lifecycle (`createAndStartReplayBuffer`,
  `stopReplayBufferOutput`, `saveReplayBuffer`), plus individual start/stop
  (`startReplayBufferIndividual`, `stopReplayBufferIndividual`).
- Settings creation is handled by `createReplayBufferSettings()`.
- The status dock includes a save button per replay buffer row and a global "Save All Replay Buffers" button.
- Replay buffer can be linked to output activation via `INTERLOCK_TYPE_REPLAY_BUFFER` or
  `INTERLOCK_TYPE_INDIVIDUAL`.
- In OBS 30.1–32.x, obs-ffmpeg registers a `ReplayBuffer.Save` hotkey on every `replay_buffer`
  output, and its assignments are never saved.
  - `createAndStartReplayBuffer()` hands the output it creates back to the caller as a weak
    reference; once every lock is released, the caller passes it to
    `unregisterReplayBufferOutputHotkey()`, so only the output's own Save hotkey (of the filter or
    the Program Output) remains.
  - Any new path that creates a `replay_buffer` output must do the same.

### Modifying Filter Video Capture

- `FilterVideoCapture` in `src/video/` captures the filter's input via GPU `gs_texrender`.
- It provides a private proxy source (`PROXY_SOURCE_ID: osi_branch_output_proxy`) for `obs_view` binding.
- The proxy source renders the captured texrender texture directly on the GPU, avoiding CPU roundtrips and enabling GPU encoder compatibility.
- Lifecycle: created in `BranchOutputFilter::setupVideoInput()` and destroyed in
  `teardownVideoInput()` (called from `ensureInfrastructure()` / `releaseInfrastructureIfIdle()`,
  respectively) when filter input mode (`useFilterInput`) is enabled.
- Key methods: `captureFilterInput()` (called from `video_render`), `renderTexture()` (called from proxy source's `video_render`), `drawCapturedTexture()` (passthrough to main output).

### Modifying Program Outputs

- Files: `plugin-program.cpp` / `.hpp` (`BranchOutputProgram`), `program-manager.cpp` / `.hpp`
  (`BranchOutputProgramManager`), and `video/main-texture-proxy.cpp` / `.hpp` (proxy path).
- Context source: `osi_branch_output_program`, an input type with `OBS_SOURCE_CAP_DISABLED`, which
  the manager creates as a private source (`obs_source_create_private()`) or loads
  (`obs_load_private_source()`).
  - Its `video_render` serves only the properties dialog preview (see Modifying Video Cropping); the
    outputs never render this source.
  - It reports the canvas size as its width and height, and the main canvas color space
    (`getMainCanvasColorSpace()`).
- Video paths (`BranchOutputProgram::setupVideoInput()`):
  - Direct path: when `canUseMainVideo()` is true, OBS's main video (`obs_get_video()`) is used as
    is.
    - `canUseMainVideo()` requires a crop covering the whole canvas, an output size equal to OBS's
      output size, and the same scaling (OBS's output is unscaled, or uses the same `scale_type`).
    - The direct path encodes on OBS's own video thread (see the FIXME in `setupVideoInput()`).
  - Proxy path: otherwise, a view whose source is a private proxy source
    (`osi_branch_output_program_proxy`, `createMainTextureProxy()`) that draws the cropped main
    texture.
  - The log shows `Video path: direct (WxH)` or `Video path: proxy (WxH)`.
  - The `obs_video_info` the video input was built from is kept; when `isVideoInputOutdated()`
    detects a change of OBS's video settings, `onIntervalTimerTimeout()` stops the outputs
    gracefully and a later tick rebuilds them.
- Persistence:
  - `BranchOutputProgramManager` is the only owner (strong references in `programs` and
    `removingPrograms`).
  - It saves to `branchOutputPrograms.json` in the current profile's folder (`version` 1, and
    `outputs` with the `obs_save_source()` result of each output), and never writes a file it could
    not read or whose version differs.
  - Frontend events (`onFrontendEvent()`):
    - `FINISHED_LOADING` loads the file, and `PROFILE_CHANGED` loads the new profile's file (see the
      profile change below).
    - `SCENE_COLLECTION_CHANGING` / `CLEANUP` suspend the outputs (`setSuspended(true)`), and
      `SCENE_COLLECTION_CHANGED` resumes them and saves.
    - `PROFILE_CHANGING` and `EXIT` save and then stop the outputs synchronously
      (`releasePrograms()` → `detach()`).
  - Profile change: on a `PROFILE_CHANGED` without `PROFILE_CHANGING` (creating, duplicating, or
    renaming a profile on OBS 31 or later), the outputs keep running and only the save target
    switches when the set of UUIDs in the new profile's file matches the running outputs; otherwise
    they are released and loaded again (`onProfileChanged()`).
  - Saving: `persistRequested` (rename, enable toggle, published settings, dialog close) and
    `outputUserEnabledChanged` call `savePrograms()` through a queued connection.
- Lifecycle (UI thread):
  - `attach()`: creates the interval timer, adds the dock rows, and syncs the hotkeys.
  - `detach()`: stops the timer, clears `attached`, stops the outputs synchronously with
    `stopOutput()` (OBS may reset the video right after), removes the dock rows, and unregisters
    the hotkeys.
  - `setSuspended()`: stops the outputs gracefully while a scene collection switches, and adds the
    rows again on resume.
  - `beginRemoval()` and `removalReady`: see the removal order under Modifying the Status Dock.
- Input availability: `isInputAvailable()` / `validateInput()` are false while suspended or
  removing, so the timer starts no output and the dock removes the rows.
- Settings:
  - The defaults come from `getProgramDefaults()` (the `resolution` default is `output`).
  - The constructor passes `inheritRecentCrop` false to `initializeSettings()`, because the crop in
    `recently.json` is in the coordinates of a filter's parent source, and then applies
    `resetTransientCheckboxes()` and `applyVideoEncoderDefaults()`.
  - For the comparison and the reconciliation on close (`settingsEquivalent()`, `nonPropertyKeys`,
    `onPropertiesDestroyed()`), see Applied settings snapshot under Important Warnings.
- Audio and hotkeys: see Modifying Audio Handling and Modifying Hotkeys.

### Modifying Video Cropping

- Cropping is configured via settings (`crop_type`, `crop_rel_*`, `crop_abs_*`) in `plugin-ui.cpp`.
- `CropRect` struct (defined in `utils.hpp`) holds resolved crop parameters.
- `BranchOutput::calculateCrop()` in `branch-output.cpp` resolves relative/absolute crop settings
  into a `CropRect`.
  - Negative values are clipped to the intersection with the source, and an empty result returns
    `std::nullopt`, which `ensureInfrastructure()` treats as a collapsed source.
- `determineOutputResolution()` accounts for cropping when computing the final output resolution.
  - The input (`getSourceResolution()`) and the crop are not rounded to even sizes; only the output
    resolution is rounded up to even by `determineOutputResolution()`, and libobs does the scaling.
- Where the crop is applied:
  - Filter, source output mode: a private scene (`obs_scene_create_private()`) holding a cropped
    scene item becomes the view's source (`BranchOutputFilter::setupVideoInput()`).
  - Filter, filter input mode: `FilterVideoCapture::setCrop()`.
  - Program Output: the proxy of the proxy path draws the cropped area (see Modifying Program
    Outputs), and a crop that does not cover the whole canvas rules out the direct path.
- `CropRectPreviewRenderer` in `src/video/` draws a live preview rectangle overlay in the filter's `video_render` callback. The preview is transient (not saved to settings) and automatically hidden when the properties dialog is closed.
  - On a Program Output, the context source's `video_render` serves only the properties dialog
    preview and draws the rectangle over the uncropped main texture
    (`BranchOutputProgram::videoRenderCallback()`).

### Modifying Streaming

- Streaming logic is implemented in `src/plugin-streaming.cpp`.
- Supports up to `MAX_SERVICES` (8) independent streaming slots via `BranchOutputStreamingContext`.
- Individual start/stop per slot via `startSingleStreamingIndividual()` / `stopSingleStreamingIndividual()`, and batch via `startStreamingIndividual()` / `stopStreamingIndividual()`.
- Per-slot user intent flags (`streamingUserEnabled[]`) are atomic and persisted via `saveCallback`.

### Modifying Stream Recording

- Stream recording logic is implemented in `src/plugin-stream-recording.cpp`.
- Supports individual start/stop via `startRecordingIndividual()` / `stopRecordingIndividual()`.
- The per-output procs for the file name format override are `override_recording_filename_format`
  and `override_replay_buffer_filename_format`; an empty `format` clears the override.
  - `BranchOutput::initializeSettings()` registers them on the context source of each output
    (filter and Program Output).
  - Their handlers are `onOverrideRecordingFilenameFormat()` (`plugin-stream-recording.cpp`) and
    `onOverrideReplayBufferFilenameFormat()` (`plugin-replay-buffer.cpp`); see Modifying the Script
    API.
- Native muxer formats map to their output types in `getNativeMuxerOutputId()` (`hybrid_mp4` → `mp4_output`, `hybrid_mov` → `mov_output`). When adding a hybrid format, also update `getFormatExt()` and `formatAcceptsCodec()` in `utils.cpp`.

### Modifying Hotkeys

- Hotkey registration and the binding cache are implemented in `src/plugin-hotkey.cpp`. The per-output-type hotkey callbacks live with their output type (`plugin-streaming.cpp` / `plugin-stream-recording.cpp` / `plugin-replay-buffer.cpp`).
- Add the new hotkey's base name and locale key as a `HotkeyText` constant at the top of
  `plugin-hotkey.cpp`, and its ID member to `BranchOutput` (`branch-output.hpp`), initialized to
  `OBS_INVALID_HOTKEY_ID` / `OBS_INVALID_HOTKEY_PAIR_ID` in the `BranchOutput` constructor.
- Wire it into the sync function of the group it belongs to (`syncFilterToggleHotkeys()` / `syncStreamingAllHotkeys()` / `syncStreamingSlotHotkeys()` / `syncRecordingHotkeys()` / `syncReplayBufferHotkeys()`), plus `captureRegisteredHotkeyBindings()` and `updateHotkeyDescriptions()`.
- A group is registered and unregistered as a unit, and its registration state is decided by one representative ID. Register the representative first and abort the group when it fails.
- When adding a new group, wire it into both `syncHotkeyGroups()` and `unregisterAllHotkeys()`.
- Register through `registerHotkeyWithRestore()` / `registerHotkeyPairWithRestore()` and unregister
  through `unregisterHotkeyWithCapture()` / `unregisterHotkeyPairWithCapture()`.
  - Only the derived classes' hooks (`registerHotkey()` / `registerHotkeyPair()`) call the libobs
    registration functions (filter: `plugin-hotkey.cpp`, Program Output: `plugin-program.cpp`).
- The filter registers its hotkeys on its parent source (`obs_hotkey_register_source()` /
  `obs_hotkey_pair_register_source()`).
- A Program Output registers its hotkeys as frontend hotkeys (`obs_hotkey_register_frontend()` /
  `obs_hotkey_pair_register_frontend()`), with names built the same way as the filter's, from the
  UUID of its private source.
  - libobs restores no assignments for frontend hotkeys, so right after registration a Program
    Output loads the profile's `[Hotkeys]` value (`loadHotkeyData()`) for each name the cache does
    not restore (`profileBindingsFor()`): the cache comes first, then `[Hotkeys]`.
- Registration happens only while `canRegisterHotkeys()` is true (see the `syncHotkeys()` warning
  under Important Warnings).
- The status dock registers the global hotkeys of its all-outputs buttons
  (`EnableAllBranchOutputsHotkey`, etc.) as frontend hotkeys and loads their assignments from the
  profile's `[Hotkeys]` (`loadHotkeys()`).

### Modifying the Script API

- Global procs, registered in `obs_module_post_load()` (`plugin-main.cpp`):
  - `osi_branch_output_get_filter_list` returns the filters only.
  - `osi_branch_output_get_output_list` returns the filters and the Program Outputs (`type` is
    `filter` or `program`).
  - The JSON shapes are documented in `API.md`.
- Both procs read, under `outputListSnapshotMutex`, the output-list snapshot
  (`std::shared_ptr<const QList<BranchOutputInfo>>`) that the dock publishes through
  `publishOutputListSnapshot()`, and touch neither the dock nor any output.
  - libobs has no API to remove a global proc, so once `moduleUnloading` is set they return the
    JSON of an empty list.
- Per-output procs: `override_recording_filename_format` / `override_replay_buffer_filename_format`
  (see Modifying Stream Recording).
- The bundled scripts are the Lua and Python scripts in `data/scripts/`.
- Do not change the names, signatures, or JSON keys of existing procs, because published scripts
  depend on them; when changing a proc, update `API.md` / `API_ja.md` as well.

---

## Important Warnings

- **Do NOT call `obs_filter_get_parent()` in the `BranchOutputFilter` constructor** — it returns `nullptr` at that point. Use `addCallback()` instead.
- **Private sources** (not visible in frontend) are intentionally excluded for filters: a filter
  whose parent is private starts no timer, is not listed in the status dock, and does not save
  `recently.json` (`addCallback()`, `validateInput()`, `updateCallback()`).
  - A Program Output is a private source by design; `BranchOutputProgramManager` connects it to the
    timer and the dock through `attach()`.
- **Applied settings snapshot** — `updateCallback()` must not restart outputs: it publishes the
  snapshot (`appliedSettings.replace()`), and the interval timer performs the restart once no
  streaming slot is in its initial start (`someStreamingsStarting()`, driven by the "starting" /
  "activate" signals).
  - A libobs reconnect episode does not raise that gate, so the restart currently reaches
    `obs_output_stop()` on a reconnecting output (see the FIXME at the `restartOutput()` call in
    `onIntervalTimerTimeout()`).
  - Output start and the output / encoder / service settings built for it read
    `appliedSettings.get()`, because the live settings hold unapplied edits of the properties
    dialog.
  - `obs_source_get_settings()` remains correct where the live object itself is required:
    - `getProperties()` of both classes (the properties view edits that object, so a copy would not
      be saved; both set the transient checkboxes of the live settings to false).
    - The crop preview in `videoTickCallback()` of both classes (it must follow unapplied edits).
    - The settings that `BranchOutputFilter::addCallback()` / `updateCallback()` and
      `BranchOutputProgram::attach()` hand to `syncHotkeys()`, which match the snapshot in the keys
      that decide the hotkey groups (`BranchOutputProgram::updateCallback()` instead hands its
      queued `syncHotkeys()` the snapshot current when it runs, `appliedSettings.get()`).
    - `BranchOutputProgram::openSettings()`, which applies `pinOutputLineup()` and
      `applyVideoEncoderDefaults()` before the dialog opens, and `onPropertiesDestroyed()`.
  - A Program Output compares before publishing (`settingsEquivalent()`): `updateCallback()`
    publishes nothing when the new settings equal the current snapshot, so OK without changes does
    not restart the output.
    - The comparison ignores `nonPropertyKeys`, `streaming_output_enabled_N`, and the key order, and
      compares the `pinOutputLineup()` keys by their effective values.
    - The filter publishes on every call without comparing.
  - A Program Output reconciles on close (`onPropertiesDestroyed()`): the standard properties
    dialog's Cancel restores the live settings without calling `update`, so when the last properties
    object made by `getProperties()` is destroyed, it calls `updateCallback()` again with the live
    settings (a no-op by the comparison when nothing changed) and requests a save.
  - The status dock (`addOutput()`, `openOutputFolder()`) reads the snapshot through
    `getAppliedSettings()`, so its rows and folder button follow the last applied settings
    (`appliedSettings`), not `activeSettings`, the snapshot the running outputs were built from
    (recorded by `ensureInfrastructure()`); the two differ while a restart is deferred.
- **Encoder compatibility** — The plugin maps "simple" encoder names to actual encoder IDs, with version-specific fallbacks (OBS 30 vs OBS 31). See `getSimpleVideoEncoder()` in `utils.hpp`.
- **Memory management** — Use OBS RAII wrappers. Raw `bfree()` / `obs_data_release()` calls are error-prone.
- **`.gitignore` uses allowlist pattern** — New top-level files/directories must be explicitly un-ignored with `!` prefix.
- **FilterVideoCapture proxy source** — The proxy source type (`osi_branch_output_proxy`) must be registered at module load via `FilterVideoCapture::createProxySourceInfo()`. The proxy source is private and intentionally not visible in the OBS frontend.
- **Program Output source types** — `osi_branch_output_program`
  (`BranchOutputProgram::createProgramInfo()`) and `osi_branch_output_program_proxy`
  (`createMainTextureProxySourceInfo()`) must be registered at module load.
  - Their sources are private, and the types carry `OBS_SOURCE_CAP_DISABLED` so that they never
    appear in OBS's add-source menu (`osi_branch_output_proxy` does the same).
  - Only `BranchOutputProgramManager` creates and owns Program Output context sources
    (`obs_source_create_private()` / `obs_load_private_source()`).
- **Video output ownership** — `infrastructureReady` is set when `ensureInfrastructure()` completes
  and cleared by `releaseInfrastructureIfIdle()` (guarded by `outputMutex`); test it, not `view`, to
  tell whether the infrastructure exists, because `view` stays null on the direct path.
  - `videoOutputOwned` is set only while the instance owns `view` and the `video_t` obtained from
    it.
    - Only an instance that has it set stops its `video_t` (`video_output_stop()`) and removes the
      view (`obs_view_remove()`).
    - Even an owner skips `video_output_stop()` while `isVideoInputOutdated()` is true, because
      `obs_reset_video()` has already freed that `video_t`.
  - The direct path of a Program Output uses OBS's main video (`obs_get_video()`, with
    `videoOutputOwned` false), which must be neither stopped nor removed.
- **Hotkey name strings** (`EnableFilter.<uuid>` etc.) must not be changed. Existing user assignments are matched by name.
  - Program Outputs use the same names with the UUID of their private source, and the profile's
    `[Hotkeys]` values are matched by these names as well.
- **Never call a libobs hotkey API (including `obs_hotkey_update_atomic()`) while holding `pluginMutex` / `outputMutex` / `audioMutex` / `AppliedSettings::mutex`.** libobs invokes hotkey callbacks while holding the hotkey mutex, and some of them take `outputMutex`. Lock order: hotkey mutex → `pluginMutex` → `outputMutex` → `audioMutex` → `AppliedSettings::mutex`. `AppliedSettings::replace()` / `get()` run under the hotkey mutex and under `outputMutex`, so they must not take any other mutex or call a libobs hotkey API.
- **Hotkey register/unregister/save/load/description updates and every access to `hotkeyBindingsCache` must run inside an `obs_hotkey_update_atomic()` callback.** `obs_hotkey_set_description()` and `obs_hotkey_pair_set_descriptions()` take no lock of their own.
- **`syncHotkeys()` registers nothing while `canRegisterHotkeys()` is false** — do not remove that
  early return or the checks behind it.
  - `BranchOutputFilter::canRegisterHotkeys()` rejects a private parent, because
    `obs_hotkey_register_source()` rejects a private parent while
    `obs_hotkey_pair_register_source()` accepts one, which would break the representative-ID
    registration check.
    - `sourceIsPrivate()` enumerates sources, so the check runs outside the hotkey mutex (before
      `obs_hotkey_update_atomic()`).
  - `BranchOutputProgram` registers only while `attached && !removing`, checked again under the
    hotkey mutex (`beginHotkeyRegistration()`).
    - `detach()` clears `attached` before `unregisterAllHotkeys()`, so no registration outlives the
      unregistration (frontend hotkeys are not unregistered when a source is destroyed).
- **Recording format check comes first** — Every path that starts a recording calls `validateRecordingFormat()` before `ensureInfrastructure()` and before creating the recording output. It rejects a native muxer format whose output type this OBS does not register, and Hybrid MOV with a codec `mov_output` cannot write (it writes a broken file without an error). It reads the codecs from the `video_encoder` / `audio_encoder` IDs in the settings, so it assumes every encoder bound to the recording is created from those two settings.
- **Status table accessibility factory** — Call `OutputStatusTable::installAccessibilityFactory()` in `obs_module_post_load()` before the status dock is created, and `OutputStatusTable::removeAccessibilityFactory()` in `obs_module_unload()` after the dock is removed, so the status table never gets Qt's `QAccessibleTable` (QTBUG-149612 crash) and no plugin function stays registered in Qt after unload. Keep `Q_OBJECT` on `OutputStatusTable`: the factory matches its class name.

---

## Agent Teams

### Communication Language

- Agents must respond in the same language the user is using (e.g., reply in Japanese if the user writes in Japanese, in English if the user writes in English).

### Team Leader Policy

- The team leader must focus exclusively on orchestrating teammates.

### Team Creation Policy

- Team size should be 3–5 members, selected from the cast below based on the task.
- Each teammate must work on different files to avoid edit conflicts.
- Start with research and review, then launch the team for parallel execution.
- **Do not use subagents for tasks that can be handled by agent teams.**

  **Exception**: Use subagents for parallel reviews.

### Available Agents

Custom agents are defined under `.claude/agents/`. Available specialists: `cpp-sensei`, `obs-sensei`, `qt-sensei`, `network-sensei`, `translation-sensei`, `av-sensei`, `devops-sensei`, `python-sensei`, `lua-sensei`.

See each agent's file in `.claude/agents/` for its specific expertise and responsibilities.
