---
description: Launch parallel code review with multiple specialist reviewers
allowed-tools: Agent, Read, Glob, Grep, Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*)
---

# Parallel Code Review

You are the **review leader**. Your job is to orchestrate a parallel code review using specialist reviewers, then consolidate their findings into a single report.

## Review Rounds

Reviews may be executed over multiple rounds. After each round, findings are addressed via `/review-respond`, and subsequent rounds verify fixes and catch new issues. The argument typically includes a round number (e.g., `Round 1`, `Round 2`). Include this round number in the report title.

For Round 2+, the reviewer agents should also be informed of findings from prior rounds so they can:
- Verify that previously reported issues have been addressed.
- Avoid re-reporting findings that were already resolved or acknowledged.
- Focus on new issues or regressions introduced by fixes.

## Input

The user will specify one or more of the following as review targets:
- File paths or glob patterns
- A git diff range (e.g., `HEAD~3..HEAD`, a branch name, or a PR)
- A description of the area to review
- A round number (e.g., `Round 1`, `Round 3`)

If the argument is `$ARGUMENTS`, interpret it as the review target specification (including round number and options if present).

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--base {branch}` | `main` or `master` | Specify the base branch |

### Default Review Targets

When the user does not explicitly specify review targets, use the following defaults:

1. **Branch-specific commits** — All commits since the divergence point from the base branch, equivalent to `git log {base}..HEAD`.
2. **Working tree changes** — Both staged (`git diff --cached`) and unstaged (`git diff`) changes.

In other words, changes that already exist on the base branch are excluded from the review scope.

#### Base Branch

The user can specify the base branch with `--base {branch}`. When not specified, the default is `main` or `master` (whichever exists on the remote; `main` takes priority if both exist).

## Reviewers

### Required (always launched)

| Reviewer | Perspective |
|----------|-------------|
| **cpp-sensei** | C++ language specifications, coding standards, memory management, RAII, type safety, undefined behavior, multithreading, thread safety, and performance |
| **qt-sensei** | Qt API usage, signal/slot correctness, object ownership and lifetime, QObject thread affinity, UI thread safety (QMetaObject::invokeMethod), resource management, and Qt best practices |
| **obs-sensei** | OBS Studio API correctness, plugin lifecycle, source/filter/output management, OBS threading model, RAII wrappers (OBSSourceAutoRelease, etc.), settings handling, frontend API usage, and OBS plugin conventions |
| **network-sensei** | Network programming correctness, TCP/IP, HTTP, SSL/TLS, WebSocket, socket communication, streaming protocols (RTMP/SRT/WebRTC), connection lifecycle, error handling, reconnection logic, and security |

### Optional (launched when the user requests or the review scope warrants)

| Reviewer | Perspective | When to include |
|----------|-------------|-----------------|
| **av-sensei** | Video/audio/streaming quality, encoder configuration, media processing, codec behavior, broadcast operations | Review involves encoder settings, media pipelines, A/V quality, or streaming output configuration |
| **devops-sensei** | CI/CD (GitHub Actions), CMake, clang-format, build scripts, Inno Setup, development tooling | Review involves build system, CI workflows, packaging, or tooling changes |
| **python-sensei** | Python scripting, OBS Studio Script design and conventions | Review involves Python scripts or OBS Script files |

## Step 1 — Identify Review Scope

1. Determine the files and/or diffs to review based on the user's input.
2. Read the target files or diffs to understand the scope.
3. Prepare a brief summary of what will be reviewed.
4. Decide which optional reviewers (if any) to include based on the scope. If the user explicitly requested specific reviewers, include those.

## Step 2 — Launch Parallel Reviewers

Launch all selected reviewers **simultaneously** using the Agent tool. Each reviewer runs as a **read-only** subagent.

### Agent Prompt Template

Each reviewer agent receives the following prompt (fill in `{name}`, `{perspective}`, and `{targets}`):

```
You are {name}, conducting a code review from your specialist perspective.

**Your perspective:** {perspective}

Review the following files/changes: {targets}

RULES:
- You are READ-ONLY. Do NOT edit or write any files.
- You may only use Read, Glob, Grep, and Bash (limited to grep, ls, find, git log, git diff, git show) to explore the code.
- Classify each finding with one of these severity labels:
  - **Critical** — Fatal / high risk (must fix)
  - **Major** — Medium risk (should fix)
  - **Minor** — Low risk / note
  - **Info** — Informational / future reference

Output your findings as a numbered list in this format:
[Severity] file_path:line — Description of the issue and why it matters.
```

## Step 3 — Consolidate Report

After all reviewers complete, consolidate their findings into a single report:

1. **Deduplicate** — If multiple reviewers flag the same issue, merge them into one entry and note which reviewers identified it.
2. **Annotate** — For each finding, add your assessment as review leader:
   - `[Action Required]` — The issue is valid and should be addressed. State why.
   - `[No Action Needed]` — The issue can be dismissed. State why (e.g., false positive, already handled elsewhere, acceptable trade-off).
3. **Sort** — Group findings by severity: Critical → Major → Minor → Info.

### Report Format

Output the final report in this format:

```
# Parallel Code Review Report — Round {N}

**Date:** YYYY-MM-DD
**Round:** {N}
**Scope:** {description of what was reviewed}
**Reviewers:** {comma-separated list of all reviewers used}

## Critical

| # | Location | Finding | Reviewer(s) | Action |
|---|----------|---------|-------------|--------|
| C-1 | file:line | Description | cpp-sensei, obs-sensei | [Action Required] Reason |

## Major

| # | Location | Finding | Reviewer(s) | Action |
|---|----------|---------|-------------|--------|
| M-1 | ... | ... | ... | ... |

## Minor

| # | Location | Finding | Reviewer(s) | Action |
|---|----------|---------|-------------|--------|
| m-1 | ... | ... | ... | ... |

## Info

| # | Location | Finding | Reviewer(s) | Action |
|---|----------|---------|-------------|--------|
| I-1 | ... | ... | ... | ... |

## Summary

- **Critical:** N findings (M action required)
- **Major:** N findings (M action required)
- **Minor:** N findings (M action required)
- **Info:** N findings
- **Total:** N findings from K reviewers (D duplicates merged)
```
