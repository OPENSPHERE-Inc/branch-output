---
name: parallel-review
description: Launch parallel code review with multiple specialist reviewers
allowed-tools: Agent, Read, Glob, Grep, Bash(.claude/scripts/fetch-diff.sh:*), Bash(grep:*), Bash(ls:*), Bash(find:*), Bash(git log:*), Bash(git diff:*), Bash(git show:*), Bash(rm:*)
---

# Parallel Code Review

You are the **review leader**. Your role is to orchestrate parallel code reviews using specialist reviewers and consolidate each reviewer's findings into a single report.

## Round Number

If the arguments include a round number (e.g., `Round 1`, `Round 2`), reflect it in the report title.

## Input

The user specifies one or more of the following as the review target:
- File paths or glob patterns
- A git diff range (e.g., `HEAD~3..HEAD`, branch name, PR)
- A description of the area to review

If the argument is `$ARGUMENTS`, interpret it as the review target specification (including round number and options).

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--base {branch}` | `main` or `master` | Specifies the base branch |

### Default Review Targets

If the user does not explicitly specify a review target, use the following as the default targets:

1. **Commits unique to the current branch** — All commits since the divergence point from the base branch (equivalent to `git log {base}..HEAD`).
2. **Working tree changes** — Both staged (`git diff --cached`) and unstaged (`git diff`) changes.

In other words, changes that already exist in the base branch are excluded from review.

If a base branch is not specified via `--base`, use `main` or `master` if either exists on the remote (prefer `main` when both exist).

## Reviewers

### Required (always launched)

| Reviewer | Perspective |
|----------|-------------|
| **cpp-sensei** | C++ language specification, coding conventions, memory management, RAII, type safety, undefined behavior, multithreading, thread safety, performance |
| **qt-sensei** | Qt API usage, signal/slot correctness, object ownership and lifetime, QObject thread affinity, UI thread safety (QMetaObject::invokeMethod), resource management, Qt best practices |
| **obs-sensei** | OBS Studio API correctness, plugin lifecycle, source/filter/output management, OBS threading model, RAII wrappers (OBSSourceAutoRelease, etc.), settings handling, frontend API usage, OBS plugin conventions |
| **network-sensei** | Networking correctness, TCP/IP, HTTP, SSL/TLS, WebSocket, socket communication, streaming protocols (RTMP/SRT/WebRTC), connection lifecycle, error handling, reconnection logic, security |

### Optional (launched based on user request or review scope)

| Reviewer | Perspective | Inclusion Criteria |
|----------|-------------|--------------------|
| **av-sensei** | Video/audio/streaming quality, encoder settings, media processing, codec behavior, distribution operations | Reviews involving encoder settings, media pipelines, A/V quality, or streaming output configuration |
| **devops-sensei** | CI/CD (GitHub Actions), CMake, clang-format, build scripts, Inno Setup, development tooling | Reviews involving the build system, CI workflows, packaging, or tool changes |
| **python-sensei** | Python scripting, OBS Studio script design and conventions | Reviews involving Python scripts or OBS Script files |
| **lua-sensei** | Lua scripting, OBS Studio Script (Lua) design and conventions | Reviews involving Lua scripts or OBS Lua Script files |

## Step 1 — Identify Review Scope and Fetch Diff

1. Based on the user's input, identify the files or diff to review.
2. Read the target files or diff to grasp the scope.
3. Produce a concise summary of the review content.
4. Decide whether to add optional reviewers based on the scope. If the user has explicitly requested specific reviewers, include them.
5. **Fetch diff information via script** — Run the provided script to collect all diff data atomically. This guarantees every section — including unstaged changes — is always written to the output file:
   - Generate an output file path: `/tmp/parallel-review-diff-{timestamp}.txt`
   - Run the script:
     ```
     .claude/scripts/fetch-diff.sh {base} {output-file-path}
     ```
   - The script writes the following sections in order:
     Changed Files / Commit Log / Commit Diff (`{base}..HEAD`) / Staged Changes / Unstaged Changes
6. **Decide how to pass the diff to reviewers based on file size:**
   - **Less than 1,000 lines:** Read the file and embed its contents directly in the reviewer prompt.
   - **1,000 lines or more:** Pass the **file path** to reviewers, who will read it via Read.

## Step 2 — Launching Parallel Reviewers

Launch all selected reviewers **simultaneously** using the Agent tool. Each reviewer runs as a **read-only** sub-agent.

### Agent Prompt Template

Pass the following prompt to each reviewer agent (filling in `{name}`, `{perspective}`, `{targets}`, `{base}`, and `{diff_content_or_path}`):

```
You are {name}. Conduct a code review from your specialist perspective.

**Your perspective:** {perspective}

Review target:
- Scope: {targets}
- Base branch: {base}

Diff and context (pre-fetched by the review leader via script):
{diff_content_or_path}
(Contains these sections: Changed Files / Commit Log / Commit Diff / Staged Changes / Unstaged Changes.
For under 1,000 lines, the contents are embedded above. For 1,000+ lines, a file path is shown — read it via Read.)

Rules:
- You are read-only. Do not edit or write any files under any circumstances.
- **The diff has been provided above. You do not need to re-run `git diff` / `git log` / `git show`.** If you need surrounding code (context lines around changes, callers, related files, etc.), use Read.
- For code investigation, you may use only Read, Glob, Grep, and Bash (limited to grep, ls, find).
- Tag each finding with one of the following severity labels:
  - **Critical** — Fatal / high risk (must fix)
  - **Major** — Medium risk (should fix)
  - **Minor** — Low risk / advisory
  - **Info** — Informational / future reference
- When deciding what is worth flagging, follow the rules in `.claude/rules/review-pedantry.md` (auto-loaded).

Output findings as a numbered list in the following format:
[Severity] file_path:line — Description of the issue and why it matters.
```

## Step 3 — Report Integration

Once all reviewers have completed, consolidate their findings into a single report:

1. **Deduplicate** — When multiple reviewers point out the same issue, merge them into a single entry and record which reviewers identified it.
2. **Reorder** — Group by severity: Critical → Major → Minor → Info.

**Triage of whether to act on findings is out of scope for this skill.** The review leader focuses solely on deduplicating and ordering raw findings.

### Report Format

Produce the final report in the following format:

```markdown
# Parallel Code Review Report — Round {N}

- **Date:** YYYY-MM-DD
- **Round:** {N}
- **Scope:** {description of the review target}
- **Reviewers:** {comma-separated list of all reviewers used}

## Critical

### C-1 — `file.cpp:42`

- **Reviewers:** cpp-sensei, obs-sensei

**Finding:**

Description of the finding. May span multiple paragraphs and include code snippets, rationale, and references.

---

### C-2 — `file.cpp:88`

...

---

## Major

### M-1 — `other.cpp:120`

- **Reviewers:** qt-sensei

**Finding:**

...

---

## Minor

No findings

---

## Info

### I-1 — `bar.cpp:7`

...

---

## Summary

- **Critical:** N
- **Major:** N
- **Minor:** N
- **Info:** N
- **Total:** N findings from K reviewers (D duplicates merged)
```

### Format Rules

- Each finding is its own subsection with a `### {finding-id} — `{location}`` heading.
- List metadata (Reviewers) as bullets per finding, then write the finding under a bold "Finding:" label.
- Separate findings with a `---` horizontal rule. **Do not output Status lines** (out of scope for this skill).
- Severity sections (`## Critical` / `## Major` / `## Minor` / `## Info`) with no findings must **not be omitted**: emit the heading and write `No findings` in the body.

## Step 4 — Clean Up Temporary Files

If the diff was saved to a temporary file in Step 1 item 6, delete that file after report integration completes. The diff is no longer needed once all reviewers have finished.
