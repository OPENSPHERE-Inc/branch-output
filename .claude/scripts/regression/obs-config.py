"""Read or edit a scene collection in the config folder of a closed OBS instance.

usage: obs-config.py --config DIR [--collection NAME] COMMAND ...

  scripts            print the collection's Tools > Scripts entries (modules.scripts-tool)
  add-script PATH    append a script with empty settings
  hotkeys            print the hotkey bindings of every Branch Output filter: those saved on the
                     parent source and those in the filter's own cache (hotkey_bindings)

--config is the instance's obs-studio config folder. --collection defaults to BORegression.
OBS rewrites the collection when it exits, so edit only while it is closed.
"""
import argparse
import json
import os
import sys


def keys(bindings):
    combos = ("+".join([m for m in ("control", "alt", "shift", "command") if b.get(m)] + [b.get("key", "?")])
              for b in bindings or [])
    return ",".join(combos) or "-"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    parser.add_argument("--collection", default="BORegression")
    parser.add_argument("command", choices=("scripts", "add-script", "hotkeys"))
    parser.add_argument("path", nargs="?")
    opts = parser.parse_args()

    path = os.path.join(opts.config, "basic", "scenes", f"{opts.collection}.json")
    with open(path, encoding="utf-8-sig") as f:
        data = json.load(f)

    if opts.command == "hotkeys":
        for src in data["sources"]:
            for flt in src.get("filters") or []:
                if flt.get("id") != "osi_branch_output":
                    continue
                suffix = "." + flt["uuid"]
                parent = src.get("hotkeys") or {}
                cache = (flt.get("settings") or {}).get("hotkey_bindings") or {}
                print(f"filter '{flt['name']}' on '{src['name']}' enabled={flt.get('enabled')}")
                print("  parent:", "; ".join(f"{n[:-len(suffix)]}={keys(parent[n])}"
                                             for n in sorted(parent) if n.endswith(suffix)))
                print("  cache :", "; ".join(f"{n.split('.')[0]}={keys(v)}" for n, v in sorted(cache.items())))
        return

    scripts = data.setdefault("modules", {}).setdefault("scripts-tool", [])
    if opts.command == "add-script":
        if not opts.path:
            sys.exit("add-script needs PATH")
        scripts.append({"path": os.path.abspath(opts.path).replace("\\", "/"), "settings": {}})
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=1, ensure_ascii=False)
    print(json.dumps(scripts, indent=1, ensure_ascii=False))


if __name__ == "__main__":
    main()
