"""obs-websocket command line for the regression skills. Needs the obsws-python package.

usage: obsws.py --config DIR COMMAND ...

  req REQUEST ['JSON']                    send any request, print the response data
  filter-get SOURCE FILTER                print a filter's settings
  filter-set SOURCE FILTER 'JSON'         overlay settings on a filter
  filter-enable SOURCE FILTER true|false  enable or disable a filter
  filter-list SOURCE                      list a source's filters
  filter-rename SOURCE FILTER NEW         rename a filter
  studio PROGRAM PREVIEW                  enter Studio Mode, set the Program scene, then the Preview scene
                                          waits for each transition; exits non-zero unless both scenes match
  hotkeys                                 count ReplayBuffer.Save hotkeys and list Branch Output hotkey names

DIR is the instance's obs-studio config folder given to fixture.py: the port and the password
are read from it.
"""
import argparse
import configparser
import json
import logging
import os
import sys
import time

try:
    import obsws_python as obs
except ImportError:
    sys.exit("obsws-python is missing: install it (pip install obsws-python), for example in a virtual environment")


def server_settings(cfg):
    """Return the port and the password of the obs-websocket server that fixture.py set up in cfg."""
    path = os.path.join(cfg, "plugin_config", "obs-websocket", "config.json")
    try:
        if os.path.isfile(path):  # OBS 31 and later
            with open(path, encoding="utf-8-sig") as f:
                ws = json.load(f)
            keys = ("server_enabled", "auth_required", "server_port", "server_password")
            enabled, auth, port, password = (ws.get(key) for key in keys)
        else:  # OBS 30.x
            path = os.path.join(cfg, "global.ini")
            ini = configparser.ConfigParser(delimiters=("=",), interpolation=None, strict=False)
            ini.read(path, encoding="utf-8-sig")
            ws = ini["OBSWebSocket"] if ini.has_section("OBSWebSocket") else {}
            enabled = ws.get("ServerEnabled") == "true"
            auth = ws.get("AuthRequired") == "true"
            port = int(ws.get("ServerPort") or 0)
            password = ws.get("ServerPassword")
    except (OSError, ValueError, configparser.Error) as e:
        sys.exit(f"cannot read the obs-websocket settings in {path}: {e}: run fixture.py first")
    if not (enabled is True and auth is True and isinstance(port, int) and 0 < port < 65536 and password):
        sys.exit(f"{path} has no obs-websocket server with a password: run fixture.py first")
    return port, password


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--config", required=True, metavar="DIR")
    parser.add_argument("command")
    parser.add_argument("args", nargs="*")
    opts = parser.parse_args()
    port, password = server_settings(opts.config)
    logging.disable(logging.CRITICAL)

    try:
        client = obs.ReqClient(host="127.0.0.1", port=port, password=password, timeout=15)
    except Exception as e:
        sys.exit(f"cannot connect to obs-websocket on port {port}: {type(e).__name__}: {e}")

    def send(request, data=None):
        try:
            return client.send(request, data, raw=True) or {}
        except obs.error.OBSSDKRequestError as e:
            sys.exit(f"{request} failed: {e}")

    def show(data):
        print(json.dumps(data, indent=1, ensure_ascii=False))

    def poll(check, timeout):
        deadline = time.monotonic() + timeout
        while not check():
            if time.monotonic() >= deadline:
                return False
            time.sleep(0.1)
        return True

    cmd, args = opts.command, opts.args
    if cmd == "req":
        show(send(args[0], json.loads(args[1]) if len(args) > 1 else None))
    elif cmd == "filter-get":
        show(send("GetSourceFilter", {"sourceName": args[0], "filterName": args[1]}))
    elif cmd == "filter-set":
        send("SetSourceFilterSettings", {"sourceName": args[0], "filterName": args[1],
                                         "filterSettings": json.loads(args[2]), "overlay": True})
        print("ok")
    elif cmd == "filter-enable":
        if args[2] not in ("true", "false"):
            sys.exit(f"filter-enable: expected true or false, got {args[2]!r}")
        send("SetSourceFilterEnabled", {"sourceName": args[0], "filterName": args[1],
                                        "filterEnabled": args[2] == "true"})
        print("ok")
    elif cmd == "filter-list":
        for f in send("GetSourceFilterList", {"sourceName": args[0]})["filters"]:
            print(f["filterName"], f["filterKind"], "enabled" if f["filterEnabled"] else "disabled")
    elif cmd == "filter-rename":
        send("SetSourceFilterName", {"sourceName": args[0], "filterName": args[1], "newFilterName": args[2]})
        print("ok")
    elif cmd == "studio":
        def transition_ended():
            return send("GetCurrentSceneTransitionCursor")["transitionCursor"] >= 1.0

        def wait_transition():
            if not poll(transition_ended, 10):
                sys.exit("studio: the scene transition did not end within 10 s")

        send("SetStudioModeEnabled", {"studioModeEnabled": True})
        # OBS drops a Program change requested during a transition, yet reports the new scene.
        wait_transition()
        send("SetCurrentProgramScene", {"sceneName": args[0]})
        # A Studio Mode transition ends by swapping the old Program into the Preview
        # (SwapScenesMode), so the Preview is set only after the transition has ended.
        wait_transition()
        time.sleep(0.2)  # the swap is queued to the UI thread just after the cursor reaches 1.0
        send("SetCurrentPreviewScene", {"sceneName": args[1]})
        poll(lambda: send("GetCurrentPreviewScene")["currentPreviewSceneName"] == args[1], 5)
        program = send("GetCurrentProgramScene")["currentProgramSceneName"]
        preview = send("GetCurrentPreviewScene")["currentPreviewSceneName"]
        print(f"program={program} preview={preview}")
        if program != args[0] or preview != args[1]:
            sys.exit(f"studio: requested program={args[0]} preview={args[1]}")
    elif cmd == "hotkeys":
        names = send("GetHotkeyList")["hotkeys"]
        branch = sorted(n for n in names if "." in n and len(n.rsplit(".", 1)[1]) == 36)
        print(f"ReplayBuffer.Save x{names.count('ReplayBuffer.Save')}")
        print("\n".join(branch))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
