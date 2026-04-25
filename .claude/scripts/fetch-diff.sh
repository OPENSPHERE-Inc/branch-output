#!/usr/bin/env bash
# fetch-diff.sh — Fetch all git diff sections for parallel-review.
# Usage: bash <path>/fetch-diff.sh <base-branch> <output-file>
#
# Writes the following sections to <output-file>:
#   === Changed Files (<base>..HEAD) ===
#   === Commit Log (<base>..HEAD) ===
#   === Commit Diff (<base>..HEAD) ===
#   === Staged Changes ===
#   === Unstaged Changes ===

BASE="${1:?Error: base branch argument required}"
OUT="${2:?Error: output file path argument required}"

{
    printf '=== Changed Files (%s..HEAD) ===\n' "${BASE}"
    git diff --name-status "${BASE}..HEAD"
    printf '\n'

    printf '=== Commit Log (%s..HEAD) ===\n' "${BASE}"
    git log "${BASE}..HEAD" --oneline
    printf '\n'

    printf '=== Commit Diff (%s..HEAD) ===\n' "${BASE}"
    git diff "${BASE}..HEAD"
    printf '\n'

    printf '=== Staged Changes ===\n'
    git diff --cached
    printf '\n'

    printf '=== Unstaged Changes ===\n'
    git diff
    printf '\n'
} > "${OUT}"
