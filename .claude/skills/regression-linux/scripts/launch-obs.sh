#!/usr/bin/env bash
# launch-obs.sh — Start OBS on the virtual display :99 in the background, isolated from
# the user's session: X11 instead of the host's Wayland, no input method, no connection
# to the host's sound server (nothing captured from or played to it), English UI, and
# its config under <work>/config.
# Usage: launch-obs.sh <obs-install> <work> [obs arguments...]
# Prints the PID. Instead of opening anything, xdg-open calls are appended to
# <work>/xdg-open.log.

set -euo pipefail

usage="usage: launch-obs.sh <obs-install> <work> [obs arguments...]"
install_dir="${1:?${usage}}"
work="${2:?${usage}}"
shift 2

if [[ ! -S /tmp/.X11-unix/X99 ]]; then
    echo "Error: display :99 is not running (start it with display.sh)" >&2
    exit 1
fi

mkdir -p "${work}/config" "${work}/bin"
shim="${work}/bin/xdg-open"
if [[ ! -x "${shim}" ]]; then
    cat >"${shim}" <<'EOF'
#!/usr/bin/env bash
echo "$(date '+%F %T') $*" >>"$(dirname "$0")/../xdg-open.log"
EOF
    chmod +x "${shim}"
fi

rm -f "${work}/obs.pid"
# --multi: OBS on Linux detects any running OBS process of the user, whatever its config.
# shellcheck disable=SC2016
setsid -f env -u WAYLAND_DISPLAY -u QT_IM_MODULE -u GTK_IM_MODULE -u XMODIFIERS \
    -u LC_ALL -u LC_MESSAGES -u LANGUAGE \
    DISPLAY=:99 XDG_SESSION_TYPE=x11 QT_QPA_PLATFORM=xcb LANG=en_US.UTF-8 \
    PULSE_SERVER=unix:/nonexistent/pulse \
    XDG_CONFIG_HOME="${work}/config" PATH="${work}/bin:${PATH}" \
    bash -c 'cd "$1" && echo $$ >"$2" && shift 2 && exec ./obs --multi "$@"' \
    _ "${install_dir}/bin" "${work}/obs.pid" "$@" >>"${work}/obs-stdout.log" 2>&1

for _ in $(seq 1 40); do
    if [[ -s "${work}/obs.pid" ]]; then
        cat "${work}/obs.pid"
        exit 0
    fi
    sleep 0.25
done
echo "Error: OBS did not start; see ${work}/obs-stdout.log" >&2
exit 1
