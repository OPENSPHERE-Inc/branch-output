"""Write the regression fixture into the config folder of a closed OBS instance.

usage: fixture.py --config DIR --media DIR --rec DIR --obs-version X.Y.Z --ws-port PORT [--text-source ID]

--config is the instance's obs-studio config folder, which OBS must already have created by
running once (global.ini must exist). The fixture:

- Profiles BORegression and BORegression2: Simple output, x264, 1280x720 at 30 fps, recording
  path <rec>/obs-rec, replay buffer on, Custom streaming service rtmp://127.0.0.1:1935/live
  (BORegression2: port 1936).
- Scene collection BORegression: scene Main with Media (quad-1k.mp4, loop), Quad (quad.png),
  Tone440 (tone440.wav, loop), and a hidden text source Name ("Alpha"); scene Other with a
  color source.
- Scene collection BORegression2: scene Scene2 with Media2 (quad-1k.mp4, loop), which has a
  Branch Output filter BO with default settings (every output type off).
- obs-websocket server on PORT (not 4455, the default port) with a password generated on each
  run (obsws.py reads it with --config), and MaxLogs=100.

--text-source defaults to text_gdiplus_v3 on Windows (text_gdiplus_v2 on OBS 30.x) and to
text_ft2_source_v2 elsewhere. <rec>/obs-rec and <rec>/bo-rec are created.
"""
import argparse
import json
import os
import secrets
import sys
import uuid


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8-sig" if path.endswith(".ini") else "utf-8") as f:
        f.write(text)


def ini_set(path, section, values):
    """Set keys of one section in an INI file, keeping every other line."""
    with open(path, encoding="utf-8-sig") as f:
        lines = f.read().splitlines()
    header = f"[{section}]"
    if header not in lines:
        lines += ["", header]
    start = lines.index(header) + 1
    end = next((i for i in range(start, len(lines)) if lines[i].startswith("[")), len(lines))
    while end > start and not lines[end - 1].strip():
        end -= 1
    for key, value in values.items():
        index = next((i for i in range(start, end) if lines[i].split("=", 1)[0] == key), None)
        if index is None:
            lines.insert(end, f"{key}={value}")
            end += 1
        else:
            lines[index] = f"{key}={value}"
    write(path, "\n".join(lines) + "\n")


def profile(cfg, name, rec, port):
    write(os.path.join(cfg, "basic", "profiles", name, "basic.ini"), f"""[General]
Name={name}

[Output]
Mode=Simple

[SimpleOutput]
FilePath={rec}/obs-rec
RecFormat2=mkv
StreamEncoder=x264
RecEncoder=x264
RecQuality=Stream
RecRB=true
RecRBTime=20
VBitrate=2500
ABitrate=160

[Video]
BaseCX=1280
BaseCY=720
OutputCX=1280
OutputCY=720
FPSType=0
FPSCommon=30

[Audio]
SampleRate=48000
ChannelSetup=Stereo
""")
    service = {"type": "rtmp_custom",
               "settings": {"server": f"rtmp://127.0.0.1:{port}/live", "key": "main", "use_auth": False}}
    write(os.path.join(cfg, "basic", "profiles", name, "service.json"), json.dumps(service))


def source(name, kind, settings, **extra):
    s = {
        "name": name, "uuid": str(uuid.uuid4()), "id": kind, "versioned_id": kind, "settings": settings,
        "mixers": 255, "sync": 0, "flags": 0, "volume": 1.0, "balance": 0.5, "enabled": True, "muted": False,
        "hotkeys": {}, "deinterlace_mode": 0, "deinterlace_field_order": 0, "monitoring_type": 0,
        "private_settings": {}, "filters": [],
    }
    s.update(extra)
    return s


def item(src, item_id, x=0.0, y=0.0, scale=1.0, visible=True):
    return {
        "name": src["name"], "source_uuid": src["uuid"], "visible": visible, "locked": False, "rot": 0.0,
        "pos": {"x": x, "y": y}, "scale": {"x": scale, "y": scale}, "align": 5, "bounds_type": 0,
        "bounds_align": 0, "bounds": {"x": 0.0, "y": 0.0}, "crop_left": 0, "crop_top": 0, "crop_right": 0,
        "crop_bottom": 0, "id": item_id, "group_item_backup": False, "scale_filter": "disable",
        "blend_method": "default", "blend_type": "normal", "show_transition": {"duration": 0},
        "hide_transition": {"duration": 0}, "private_settings": {},
    }


def scene(name, items):
    return source(name, "scene", {"id_counter": len(items), "custom_size": False, "items": items}, mixers=0)


def collection(cfg, name, scenes, sources):
    data = {
        "name": name, "current_scene": scenes[0]["name"], "current_program_scene": scenes[0]["name"],
        "scene_order": [{"name": s["name"]} for s in scenes], "sources": scenes + sources, "groups": [],
        "quick_transitions": [], "transitions": [], "saved_projectors": [], "current_transition": "Fade",
        "transition_duration": 300, "preview_locked": False, "scaling_enabled": False, "scaling_level": 0,
        "scaling_off_x": 0.0, "scaling_off_y": 0.0, "modules": {"scripts-tool": []},
    }
    write(os.path.join(cfg, "basic", "scenes", f"{name}.json"), json.dumps(data, indent=1))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    parser.add_argument("--media", required=True)
    parser.add_argument("--rec", required=True)
    parser.add_argument("--obs-version", required=True)
    parser.add_argument("--ws-port", type=int, required=True)
    parser.add_argument("--text-source")
    opts = parser.parse_args()
    if not 0 < opts.ws_port < 65536 or opts.ws_port == 4455:
        parser.error("--ws-port must be 1-65535 and not 4455, the default port another OBS may hold")

    cfg = os.path.abspath(opts.config)
    global_ini = os.path.join(cfg, "global.ini")
    if not os.path.isfile(global_ini):
        sys.exit(f"{global_ini} not found: launch and quit this OBS instance once first")
    is30 = opts.obs_version.startswith("30.")
    media = os.path.abspath(opts.media).replace("\\", "/")
    rec = os.path.abspath(opts.rec).replace("\\", "/")
    for sub in ("obs-rec", "bo-rec"):
        os.makedirs(os.path.join(rec, sub), exist_ok=True)
    text_source = opts.text_source
    if not text_source:
        if sys.platform == "win32":
            text_source = "text_gdiplus_v2" if is30 else "text_gdiplus_v3"
        else:
            text_source = "text_ft2_source_v2"

    profile(cfg, "BORegression", rec, 1935)
    profile(cfg, "BORegression2", rec, 1936)

    media_src = source("Media", "ffmpeg_source", {"local_file": f"{media}/quad-1k.mp4", "looping": True,
                                                  "restart_on_activate": False, "close_when_inactive": False})
    quad = source("Quad", "image_source", {"file": f"{media}/quad.png"})
    tone = source("Tone440", "ffmpeg_source", {"local_file": f"{media}/tone440.wav", "looping": True,
                                               "close_when_inactive": False})
    name = source("Name", text_source, {"text": "Alpha"})
    color = source("Color", "color_source_v3", {"color": 4278222848, "width": 1280, "height": 720})
    main_scene = scene("Main", [item(media_src, 1), item(quad, 2, 960.0, 0.0, 0.25), item(tone, 3),
                                item(name, 4, 20.0, 600.0, visible=False)])
    other = scene("Other", [item(color, 1)])
    collection(cfg, "BORegression", [main_scene, other], [media_src, quad, tone, name, color])

    media2 = source("Media2", "ffmpeg_source", {"local_file": f"{media}/quad-1k.mp4", "looping": True,
                                                "close_when_inactive": False})
    media2["filters"].append(source("BO", "osi_branch_output", {}, mixers=0))
    collection(cfg, "BORegression2", [scene("Scene2", [item(media2, 1)])], [media2])

    ini_set(global_ini, "General", {"MaxLogs": 100})
    password = secrets.token_urlsafe(16)
    if is30:
        ini_set(global_ini, "OBSWebSocket", {"FirstLoad": "false", "ServerEnabled": "true",
                                             "ServerPort": opts.ws_port, "AlertsEnabled": "false",
                                             "AuthRequired": "true", "ServerPassword": password})
    else:
        ws = {"alerts_enabled": False, "auth_required": True, "first_load": False, "server_enabled": True,
              "server_password": password, "server_port": opts.ws_port}
        write(os.path.join(cfg, "plugin_config", "obs-websocket", "config.json"), json.dumps(ws))
    print(f"fixture written to {cfg} (OBS {opts.obs_version}, text source {text_source}, "
          f"obs-websocket port {opts.ws_port})")


if __name__ == "__main__":
    main()
