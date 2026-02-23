# AGENTS.md — Branch Output Plugin for OBS Studio

## Project Overview

**Branch Output** is an OBS Studio plugin (shared library / module) that adds an effect filter allowing
individual sources or scenes to stream and/or record independently from the main OBS output.
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
├── .github/
│   ├── actions/             # Reusable format-check actions
│   └── workflows/           # CI workflows (build, format check, release)
├── cmake/                   # CMake helpers & platform-specific modules
├── build-aux/               # Auxiliary build scripts (format runners)
├── data/
│   └── locale/              # Translation files (en-US, ja-JP, zh-CN, ko-KR, etc.)
├── src/
│   ├── plugin-main.cpp      # Plugin entry point, BranchOutputFilter implementation
│   ├── plugin-main.hpp      # BranchOutputFilter class declaration
│   ├── plugin-ui.cpp        # OBS properties UI, filter settings UI
│   ├── plugin-support.h     # Auto-generated plugin support header
│   ├── plugin-support.c.in  # Template for plugin-support.c
│   ├── utils.cpp / .hpp     # Utility functions, encoder helpers, profile helpers
│   ├── audio/
│   │   ├── audio-capture.cpp / .hpp  # Audio capture abstraction
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
| macOS    | Xcode 15+, CMake ≥ 3.16, Qt 6 |
| Linux    | GCC / Clang, CMake ≥ 3.16, Qt 6 |

OBS Studio sources and pre-built dependencies are fetched automatically via `buildspec.json`
(obs-studio 30.1.2, obs-deps, Qt6).

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

The plugin registers an OBS **effect filter** via `obs_source_info` (filter ID: `osi_branch_output`).
The main class is `BranchOutputFilter` (declared in `plugin-main.hpp`), which is a `QObject` subclass.

### Core Responsibilities of BranchOutputFilter

| Area | Description |
|------|-------------|
| **Streaming** | Creates up to `MAX_SERVICES` (8) independent streaming outputs with dedicated services, encoders, and reconnect logic. |
| **Recording** | Supports file recording with various container formats, time/size-based splitting, pause/unpause, and chapter markers. |
| **Audio** | Manages up to `MAX_AUDIO_MIXES` audio contexts via `AudioCapture` class. Supports filter audio, per-source audio, and audio track selection. |
| **Video** | Creates an OBS view (`obs_view_t`) with a private video output for per-filter encoding and resolution control. |
| **UI** | Properties panel built via OBS properties API (`plugin-ui.cpp`). Status dock (`BranchOutputStatusDock`) shows live statistics for all filters. |
| **Hotkeys** | Registers hotkey pairs for enable/disable, split recording, pause/unpause, and chapter markers. |
| **Interlock** | Can link filter activation to OBS streaming, recording, or virtual camera states. |
| **Blanking** | Uses a private solid-color source to blank output when the parent source is inactive. |

### Threading Model

- OBS callbacks (video render, audio filter, video tick) may run on **different threads** from the UI thread.
- `pthread_mutex_t outputMutex` protects streaming/recording output state.
- `QMutex` protects audio buffers in `AudioCapture`.
- UI updates use `QMetaObject::invokeMethod` with `Qt::QueuedConnection` for thread safety.
- Settings changes are tracked via revision counters (`storedSettingsRev` / `activeSettingsRev`) to defer restarts.

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
(`run-clang-format`, `run-cmake-format`). PRs and pushes to `master` are validated.

---

## Coding Guidelines

### General Rules

1. **Follow OBS plugin conventions** — Use `obs_log()` for logging, `OBS_DECLARE_MODULE()` for entry, locale via `obs_module_text()`.
2. **RAII wrappers** — Always use `OBSSourceAutoRelease`, `OBSDataAutoRelease`, etc. instead of manual `obs_*_release()`.
3. **Thread safety** — Any data shared between OBS callbacks and UI must be guarded by mutex. Use `QMetaObject::invokeMethod` with `Qt::QueuedConnection` when calling UI methods from non-UI threads.
4. **Settings migration** — When changing settings schema, add backward-compatible migration code in the constructor (see `audio_source` migration example in `BranchOutputFilter` constructor).
5. **Qt MOC** — Classes using `Q_OBJECT` must be in headers. `AUTOMOC`, `AUTOUIC`, `AUTORCC` are enabled.

### Naming Conventions

- **Classes**: PascalCase (`BranchOutputFilter`, `AudioCapture`, `OutputTableRow`)
- **Methods**: camelCase (`startOutput`, `stopRecordingOutput`, `onIntervalTimerTimeout`)
- **Constants/Macros**: UPPER_SNAKE_CASE (`MAX_SERVICES`, `FILTER_ID`, `OUTPUT_MAX_RETRIES`)
- **Member variables**: camelCase, no prefix (`filterSource`, `videoEncoder`, `recordingActive`)
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
- **Manual testing** is required: load the plugin in OBS Studio, add the "Branch Output" filter, and verify streaming/recording behavior.
- Test on all supported platforms when possible (Windows x64, macOS, Linux).
- Verify Studio Mode compatibility (Branch Output ignores studio mode's program and outputs from preview).
- Verify that enabling and then disabling the Branch Output filter does not cause a crash.
- Verify that shutting down OBS does not crash when the Branch Output filter is inactive or active, respectively.
- Verify that switching scene collections does not crash when the Branch Output filter is inactive or active, respectively.
- Verify that no memory leaks are logged on OBS shutdown.
- Verify that mutex does not cause deadlocks.

---

## CI / CD

| Workflow | Trigger | Purpose |
|----------|---------|---------|
| `push.yaml` | Push to `master`/`main`/`release/**`, tags | Format check + build + release creation |
| `build-project.yaml` | Called by push/PR workflows | Multi-platform build (Windows, macOS, Linux) |
| `check-format.yaml` | Called by push workflow | clang-format and cmake-format validation |
| `pr-pull.yaml` | Pull requests | Build validation |
| `dispatch.yaml` | Manual dispatch | On-demand builds |

Release tags follow semver: `X.Y.Z` for stable, `X.Y.Z-beta`/`X.Y.Z-rc` for pre-releases.

---

## Common Tasks for AI Agents

### Adding a New Setting / Feature

1. Add the setting key to `BranchOutputFilter::getDefaults()` in `plugin-main.cpp`.
2. Add UI controls in `plugin-ui.cpp` (use OBS properties API: `obs_properties_add_*`).
3. Handle the setting in `startOutput()` / `stopOutput()` / `updateCallback()` as needed.
4. Add locale strings to **all** locale files under `data/locale/` (`en-US.ini`, `ja-JP.ini`, `zh-CN.ini`, `ko-KR.ini`, `de-DE.ini`, `fr-FR.ini`, `ca-ES.ini`, `ro-RO.ini`, `ru-RU.ini`, `uk-UA.ini`). `en-US.ini` is the primary (required); all others should also be updated.
5. Ensure backward compatibility — existing saved settings must still load correctly.

### Adding a New Streaming Service Slot

- The plugin supports up to `MAX_SERVICES` (8) service slots.
- Each slot has its own `BranchOutputStreamingContext` with output, service, and signals.
- Use `getIndexedPropNameFormat()` for indexed property names.

### Modifying Audio Handling

- Audio capture is abstracted through `AudioCapture` class in `src/audio/`.
- The class manages an audio buffer, supports push/pop patterns, and handles format conversion.
- Audio mixers and encoder binding are managed in `BranchOutputFilter::startOutput()`.

### Modifying the Status Dock

- `BranchOutputStatusDock` in `src/UI/` is a `QFrame`-based dock widget.
- It uses a `QTableWidget` with custom cell classes (`OutputTableCellItem`, `LabelCell`, `FilterCell`).
- Thread-safe updates via `QMetaObject::invokeMethod`.

---

## Important Warnings

- **Do NOT call `obs_filter_get_parent()` in the `BranchOutputFilter` constructor** — it returns `nullptr` at that point. Use `addCallback()` instead.
- **Private sources** (not visible in frontend) are intentionally excluded from status dock and timer registration.
- **Settings revisions** (`storedSettingsRev` / `activeSettingsRev`) exist to avoid stopping output during reconnect attempts. Do not bypass this mechanism.
- **Encoder compatibility** — The plugin maps "simple" encoder names to actual encoder IDs, with version-specific fallbacks (OBS 30 vs OBS 31). See `getSimpleVideoEncoder()` in `utils.hpp`.
- **Memory management** — Use OBS RAII wrappers. Raw `bfree()` / `obs_data_release()` calls are error-prone.
- **`.gitignore` uses allowlist pattern** — New top-level files/directories must be explicitly un-ignored with `!` prefix.
