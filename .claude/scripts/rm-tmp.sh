#!/usr/bin/env bash
# rm-tmp.sh — Delete files or directories under .claude/tmp/ only.
# Usage: bash <path>/rm-tmp.sh <path> [<path> ...]
#
# Restricts deletion to paths under the project's .claude/tmp/ directory
# so that Bash(rm:*) need not be added to the permission allowlist.
# Rejects: paths outside .claude/tmp/, paths containing '..',
# and the .claude/tmp/ root itself (only sub-paths may be deleted).
# Directories are removed recursively.

set -euo pipefail

if [ $# -eq 0 ]; then
    echo "Error: at least one path is required" >&2
    exit 2
fi

ALLOWED_PREFIX=".claude/tmp/"

for target in "$@"; do
    normalized="${target#./}"

    if [[ "${normalized}" == *..* ]]; then
        echo "Error: path containing '..' is not allowed: ${target}" >&2
        exit 1
    fi

    if [[ "${normalized}" != "${ALLOWED_PREFIX}"* ]]; then
        echo "Error: path is not under ${ALLOWED_PREFIX}: ${target}" >&2
        exit 1
    fi

    # Reject the bare .claude/tmp/ root (must have at least one path
    # component beneath it). Trailing slash is normalized away first.
    stripped="${normalized%/}"
    if [[ "${stripped}" == "${ALLOWED_PREFIX%/}" ]]; then
        echo "Error: deleting the .claude/tmp/ root itself is not allowed: ${target}" >&2
        exit 1
    fi

    rm -rf -- "${normalized}"
done
