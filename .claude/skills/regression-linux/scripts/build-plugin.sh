#!/usr/bin/env bash
# build-plugin.sh — Deploy the Branch Output build under test as a user plugin of the
# instance in <work>: <work>/config/obs-studio/plugins/osi-branch-output/.
# Usage: build-plugin.sh <obs-install> <work> [<plugin-install>]
# <plugin-install> is a folder in install layout (lib64/obs-plugins/osi-branch-output.so,
# share/obs/obs-plugins/osi-branch-output/). Without it, the current checkout is built
# against <obs-install> into <work>/plugin-build and installed into <work>/plugin-install.
# Prints the SHA-256 of the deployed osi-branch-output.so.

set -euo pipefail

usage="usage: build-plugin.sh <obs-install> <work> [<plugin-install>]"
obs_install="${1:?${usage}}"
work="${2:?${usage}}"
plugin_install="${3:-}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(git -C "${script_dir}" rev-parse --show-toplevel)"

run_logged() {
    local log="$1"
    shift
    if ! "$@" >"${log}" 2>&1; then
        tail -n 30 "${log}" >&2
        echo "Error: '$*' failed; see ${log}" >&2
        exit 1
    fi
}

if [[ -z "${plugin_install}" ]]; then
    plugin_install="${work}/plugin-install"
    # The preset's own binary folder (build_x86_64) may hold the user's configuration,
    # and the default install prefix is a system folder.
    cd "${repo_root}"
    run_logged "${work}/plugin-configure.log" cmake --preset linux-x86_64 \
        -B "${work}/plugin-build" -DCMAKE_PREFIX_PATH="${obs_install}"
    run_logged "${work}/plugin-build.log" cmake --build "${work}/plugin-build"
    run_logged "${work}/plugin-install.log" cmake --install "${work}/plugin-build" \
        --prefix "${plugin_install}"
fi

so="${plugin_install}/lib64/obs-plugins/osi-branch-output.so"
data="${plugin_install}/share/obs/obs-plugins/osi-branch-output"
if [[ ! -f "${so}" || ! -d "${data}" ]]; then
    echo "Error: ${plugin_install} is not in install layout" >&2
    exit 1
fi

dest="${work}/config/obs-studio/plugins/osi-branch-output"
mkdir -p "${dest}/bin/64bit" "${dest}/data"
cp "${so}" "${dest}/bin/64bit/"
cp -r "${data}/." "${dest}/data/"
sha256sum "${dest}/bin/64bit/osi-branch-output.so" | cut -d ' ' -f 1
