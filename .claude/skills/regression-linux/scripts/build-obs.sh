#!/usr/bin/env bash
# build-obs.sh — Build an OBS Studio release from its official source tarball into
# .claude/tmp/regression-linux-obs-<version>/install, reusing a completed build.
# The source and build trees are deleted after a successful install.
# Usage: build-obs.sh <version>
# Prints the install prefix on success.

set -euo pipefail

version="${1:?usage: build-obs.sh <version>}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(git -C "${script_dir}" rev-parse --show-toplevel)"
obs_dir="${repo_root}/.claude/tmp/regression-linux-obs-${version}"
install_dir="${obs_dir}/install"
stamp="${obs_dir}/build.ok"
asset="OBS-Studio-${version}-Sources.tar.gz"

if [[ -f "${stamp}" && -x "${install_dir}/bin/obs" ]]; then
    echo "${install_dir}"
    exit 0
fi

run_logged() {
    local log="$1"
    shift
    if ! "$@" >"${log}" 2>&1; then
        tail -n 30 "${log}" >&2
        echo "Error: '$*' failed; see ${log}" >&2
        exit 1
    fi
}

mkdir -p "${obs_dir}"
"${repo_root}/.claude/scripts/del-tmp.sh" "${obs_dir}/src" "${obs_dir}/build" "${install_dir}"

digest="$(gh release view "${version}" --repo obsproject/obs-studio --json assets \
    --jq ".assets[] | select(.name == \"${asset}\") | .digest")"
if [[ "${digest}" != sha256:* ]]; then
    echo "Error: release ${version} lists no SHA-256 digest for ${asset}" >&2
    exit 1
fi
if ! echo "${digest#sha256:}  ${obs_dir}/${asset}" | sha256sum --check --status - 2>/dev/null; then
    gh release download "${version}" --repo obsproject/obs-studio --pattern "${asset}" \
        --dir "${obs_dir}" --clobber
    echo "${digest#sha256:}  ${obs_dir}/${asset}" | sha256sum --check --quiet -
fi

mkdir -p "${obs_dir}/src"
tar -xzf "${obs_dir}/${asset}" -C "${obs_dir}/src" --strip-components=1

# The tarball has no .git, so the version must be given explicitly.
# ENABLE_RELOCATABLE gives the binaries an $ORIGIN-relative RUNPATH; without it,
# /etc/ld.so.conf.d may resolve libobs to the OBS installed on the machine.
run_logged "${obs_dir}/configure.log" cmake -S "${obs_dir}/src" -B "${obs_dir}/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX="${install_dir}" \
    -DOBS_VERSION_OVERRIDE="${version}" \
    -DENABLE_RELOCATABLE=ON \
    -DENABLE_BROWSER=OFF \
    -DENABLE_AJA=OFF \
    -DENABLE_VLC=OFF \
    -DENABLE_WEBRTC=OFF
run_logged "${obs_dir}/build.log" cmake --build "${obs_dir}/build"
run_logged "${obs_dir}/install.log" cmake --install "${obs_dir}/build"

# shellcheck disable=SC2016
if ! readelf -d "${install_dir}/bin/obs" | grep -q 'RUNPATH.*\$ORIGIN'; then
    echo "Error: ${install_dir}/bin/obs has no \$ORIGIN-relative RUNPATH" >&2
    exit 1
fi

"${repo_root}/.claude/scripts/del-tmp.sh" "${obs_dir}/src" "${obs_dir}/build"
echo "${version}" >"${stamp}"
echo "${install_dir}"
