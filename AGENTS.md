# AGENTS.md — Branch Output Plugin for OBS Studio

This file provides a brief overview for AI agents. For detailed project documentation, coding guidelines, architecture, and build instructions, see **[CLAUDE.md](CLAUDE.md)**.

## Quick Reference

- **Language**: C++17 with Qt 6
- **Build**: CMake ≥ 3.16
- **Target OBS**: ≥ 30.1.0
- **License**: GPLv2+
- **Maintainer**: OPENSPHERE Inc.

## Key Files

| File | Purpose |
|------|---------|
| `src/plugin-main.cpp` | Plugin entry point, `BranchOutputFilter` core logic |
| `src/plugin-main.hpp` | `BranchOutputFilter` class declaration |
| `src/plugin-ui.cpp` | OBS properties UI, `getDefaults()`, filter settings |
| `src/plugin-streaming.cpp` | Streaming output logic |
| `src/plugin-stream-recording.cpp` | Recording output logic |
| `src/plugin-replay-buffer.cpp` | Replay buffer output logic |
| `src/utils.cpp` / `.hpp` | Utility functions, encoder helpers |
| `src/audio/audio-capture.cpp` | Audio capture abstraction |
| `src/video/filter-video-capture.cpp` | Filter input video capture (GPU texrender proxy) |
| `src/UI/output-status-dock.cpp` | Status dock widget |
| `data/locale/*.ini` | Locale strings (10 languages) |

## Essential Rules

1. Follow OBS plugin conventions (`obs_log()`, `obs_module_text()`, RAII wrappers)
2. All user-facing strings must be localized in **all** `data/locale/*.ini` files
3. Thread safety required — use mutexes for shared state, `Qt::QueuedConnection` for UI updates
4. Code style enforced by `.clang-format` (120 columns, 4-space indent, C++17)
5. No automated tests — manual testing in OBS Studio required

For full details on architecture, coding guidelines, build instructions, CI/CD, and common tasks, refer to **[CLAUDE.md](CLAUDE.md)**.
