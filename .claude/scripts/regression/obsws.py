"""obs-websocket command line for the regression skills. Needs the obsws-python package.

usage: obsws.py [--port PORT] COMMAND ...

  req REQUEST ['JSON']                    send any request, print the response data
  filter-get SOURCE FILTER                print a filter's settings
  filter-set SOURCE FILTER 'JSON'         overlay settings on a filter
  filter-enable SOURCE FILTER true|false  enable or disable a filter
  filter-list SOURCE                      list a source's filters
  filter-rename SOURCE FILTER NEW         rename a filter
  studio PROGRAM PREVIEW                  enter Studio Mode, set the Program scene, then the Preview scene
  hotkeys                                 count ReplayBuffer.Save hotkeys and list Branch Output hotkey names

The server has no authentication (fixture.py). PORT defaults to 4455.
"""
import argparse
import json
import logging
import sys

try:
    import obsws_python as obs
except ImportError:
    sys.exit("obsws-python is missing: install it (pip install obsws-python), for example in a virtual environment")


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--port", type=int, default=4455)
    parser.add_argument("command")
    parser.add_argument("args", nargs="*")
    opts = parser.parse_args()
    logging.disable(logging.CRITICAL)

    try:
        client = obs.ReqClient(host="127.0.0.1", port=opts.port, timeout=15)
    except OSError as e:
        sys.exit(f"cannot connect to obs-websocket on port {opts.port}: {e}")

    def send(request, data=None):
        try:
            return client.send(request, data, raw=True) or {}
        except obs.error.OBSSDKRequestError as e:
            sys.exit(f"{request} failed: {e}")

    def show(data):
        print(json.dumps(data, indent=1, ensure_ascii=False))

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
        # Setting the Program scene also moves the Preview to it, so the Preview goes second.
        send("SetStudioModeEnabled", {"studioModeEnabled": True})
        send("SetCurrentProgramScene", {"sceneName": args[0]})
        send("SetCurrentPreviewScene", {"sceneName": args[1]})
        program = send("GetCurrentProgramScene")["currentProgramSceneName"]
        preview = send("GetCurrentPreviewScene")["currentPreviewSceneName"]
        print(f"program={program} preview={preview}")
    elif cmd == "hotkeys":
        names = send("GetHotkeyList")["hotkeys"]
        branch = sorted(n for n in names if "." in n and len(n.rsplit(".", 1)[1]) == 36)
        print(f"ReplayBuffer.Save x{names.count('ReplayBuffer.Save')}")
        print("\n".join(branch))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
