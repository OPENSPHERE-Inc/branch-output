---
name: format-build-verify
description: Prompt for the format & build verification sub-agent that performs format and build verification once in review-respond Step 4
template_id: 9d3c5f8a-2b71-4e94-a8c5-1f7d3b9e2c46
---

As the format & build verification owner, run format verification → build verification exactly once. Do not run the fix loop (the leader has a specialist Sub perform the fix and then relaunches this Sub). Only on failure, read the code to analyze the cause and identify the responsible specialist for the fix. Source changes are limited to formatter auto-fixes (logic changes prohibited). Read `.claude/rules/sub-agent.md` and observe the common prohibitions.

Inputs: working directory `{{tmp_dir}}`, attempt number `{{attempt_num}}` (informational only).

What to do:

1. Format verification:
   - Get the list of changed files via git. Verify C/C++ (.cpp/.hpp/.h/.c) with clang-format and CMake (CMakeLists.txt/*.cmake) with cmake-format.
   - Verification commands: `clang-format -style=file -fallback-style=none --dry-run -Werror <file>`; on violation, auto-fix with `clang-format -i -style=file -fallback-style=none <file>`. CMake is the same.

2. Build verification:
   - Choose the command for the current platform from build.ps1 / Makefile / build script, or from the CMake presets listed in CLAUDE.md.
   - Run the build, save stdout/stderr to `{{tmp_dir}}/build.log`. Determine success/failure by exit code.

3. Specialist identification on failure:
   - Read build.log and the error-source files to analyze the cause and concisely organize the fix direction (fix_guidance).
   - Specialist selection: cpp-sensei (C++ compile / link / language spec) / qt-sensei (Qt API / MOC / signal-slot) / obs-sensei (OBS Studio API) / network-sensei (TCP/IP / HTTP / SSL / RTMP / SRT / WebSocket) / av-sensei (AV / encoders / media pipeline) / devops-sensei (CMake / CI / build script / Inno Setup) / python-sensei (.py) / lua-sensei (.lua).

4. Write to `{{tmp_dir}}/format-build-result.json`.

`{{tmp_dir}}/format-build-result.json` format:

```
{
  "format": {changed_files: [...], format_violations_fixed: <int>, format_violations_remaining: <int>},
  "build": {success: <bool>, build_log_path, error_summary | null, error_files: ["src/foo.cpp:42", ...] | null, suggested_specialist | null, fix_guidance | null}
}
```

Return value: `{path, success, format_violations_fixed, summary_line (<=200 chars; e.g. "format ok / build ok" or "format ok / build failed: cpp-sensei suggested for src/foo.cpp:42"), template_id}`. Include `template_id` (Read from this template's frontmatter) verbatim in the return value.
