#!/usr/bin/env bash
# display.sh — Start or stop the virtual X display :99 (Xvfb 1280x800 with openbox).
# Usage: display.sh start|stop <work>
# PIDs and logs are kept in <work>/display/. stop kills only the recorded processes.

set -euo pipefail

action="${1:?usage: display.sh start|stop <work>}"
work="${2:?usage: display.sh start|stop <work>}"
state="${work}/display"

wait_for() {
    local _
    for _ in $(seq 1 40); do
        if "$@"; then
            return 0
        fi
        sleep 0.25
    done
    return 1
}

pid_alive() {
    [[ -s "$1" ]] && kill -0 "$(cat "$1")" 2>/dev/null
}

stop_recorded() {
    local name="$1" comm="$2" pid_file="${state}/$1.pid" pid
    pid_alive "${pid_file}" || return 0
    pid="$(cat "${pid_file}")"
    if [[ "$(ps -o comm= -p "${pid}")" != "${comm}" ]]; then
        echo "Warning: PID ${pid} is not ${comm}; left running" >&2
        return 0
    fi
    kill "${pid}"
    wait_for bash -c "! kill -0 ${pid} 2>/dev/null" || echo "Warning: ${name} (${pid}) did not exit" >&2
}

case "${action}" in
start)
    if [[ -e /tmp/.X11-unix/X99 || -e /tmp/.X99-lock ]]; then
        echo "Error: display :99 is already in use" >&2
        exit 1
    fi
    mkdir -p "${state}"
    # shellcheck disable=SC2016
    setsid -f bash -c 'echo $$ >"$1"; exec Xvfb :99 -screen 0 1280x800x24 -nolisten tcp' \
        _ "${state}/xvfb.pid" >"${state}/xvfb.log" 2>&1
    wait_for test -S /tmp/.X11-unix/X99 || { echo "Error: Xvfb did not start" >&2; exit 1; }
    # A window manager is needed for the focus and stacking of OBS dialogs.
    # shellcheck disable=SC2016
    setsid -f env -u WAYLAND_DISPLAY DISPLAY=:99 bash -c 'echo $$ >"$1"; exec openbox' \
        _ "${state}/openbox.pid" >"${state}/openbox.log" 2>&1
    wait_for pid_alive "${state}/openbox.pid" || { echo "Error: openbox did not start" >&2; exit 1; }
    echo "DISPLAY=:99 ready (Xvfb $(cat "${state}/xvfb.pid"), openbox $(cat "${state}/openbox.pid"))"
    ;;
stop)
    stop_recorded openbox openbox
    stop_recorded xvfb Xvfb
    ;;
*)
    echo "usage: display.sh start|stop <work>" >&2
    exit 2
    ;;
esac
