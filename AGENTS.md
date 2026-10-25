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
| `src/branch-output.cpp` / `.hpp` | `BranchOutput` base class (shared output logic, virtual hooks) |
| `src/plugin-main.cpp` | Module entry points, script list procs, `BranchOutputFilter` |
| `src/plugin-main.hpp` | `BranchOutputFilter` class declaration |
| `src/plugin-program.cpp` / `.hpp` | `BranchOutputProgram` (Program Output) |
| `src/program-manager.cpp` / `.hpp` | `BranchOutputProgramManager` (ownership, `branchOutputPrograms.json`) |
| `src/plugin-hotkey.cpp` | Hotkey registration sync and binding cache |
| `src/plugin-streaming.cpp` | Streaming output logic |
| `src/plugin-stream-recording.cpp` | Recording output logic |
| `src/plugin-replay-buffer.cpp` | Replay buffer output logic |
| `src/plugin-ui.cpp` | Shared property groups, `getDefaults()`, filter properties, status dock creation |
| `src/utils.cpp` / `.hpp` | Utility functions, encoder helpers |
| `src/audio/audio-capture.cpp` | Audio capture abstraction |
| `src/video/filter-video-capture.cpp` | Filter input video capture (GPU texrender proxy) |
| `src/video/main-texture-proxy.cpp` | Program Output proxy path (main texture proxy source) |
| `src/UI/output-status-dock.cpp` | Status dock widget ("+" menu, row context menu) |
| `data/locale/*.ini` | Locale strings (10 languages) |
| `data/scripts/` | Sample Lua / Python scripts |
| `API.md` | Script API reference |

## Essential Rules

1. Follow OBS plugin conventions (`obs_log()`, `obs_module_text()`, RAII wrappers)
2. All user-facing strings must be localized in **all** `data/locale/*.ini` files
3. Thread safety required — use mutexes for shared state, `Qt::QueuedConnection` for UI updates
4. Code style enforced by `.clang-format` (120 columns, 4-space indent, C++17)
5. No automated tests — manual testing in OBS Studio required

## Codex Instructions

Read the applicable file under `.codex/instructions/` before starting the corresponding work:

- Code changes: `development.md`, plus `comment.md` when comments are added or modified
- Human-facing documentation: `document.md`
- AI-facing prompts and agent definitions: `prompt.md`
- Code review: `review.md`
- Commits: `commit.md`
- CReview sub-agent work: `sub-agent.md`

Project-scoped custom agents are defined under `.codex/agents/`. When delegating, select the narrowest specialist whose
description matches the task and keep ownership boundaries explicit.

For full details on architecture, coding guidelines, build instructions, CI/CD, and common tasks, refer to **[CLAUDE.md](CLAUDE.md)**.
