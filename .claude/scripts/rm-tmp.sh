#!/usr/bin/env bash
# rm-tmp.sh — Delete files under .claude/tmp/ only.
# Usage: bash <path>/rm-tmp.sh <path> [<path> ...]
#
# Restricts deletion to files under the project's .claude/tmp/ directory
# so that Bash(rm:*) need not be added to the permission allowlist.
# Rejects: paths outside .claude/tmp/, paths containing '..', and directories.

set -euo pipefail

if [ $# -eq 0 ]; then
    echo "Error: at least one file path is required" >&2
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

    if [ -d "${normalized}" ]; then
        echo "Error: directory deletion is not allowed: ${target}" >&2
        exit 1
    fi

    rm -f -- "${normalized}"
done
