"""Probe recordings with ffprobe / ffmpeg.

usage: probe.py [--colors] [--tones] [--gaps] [--window T0-T1 ...] FILE [FILE ...]

Always: video codec, size, frame rate, duration, audio stream count, major brand, chapter count.
  --colors  colors at the four quadrant centers of the frame at 1 s (quad.png is red, green / blue, yellow)
  --tones   mean level of the 1 kHz and 440 Hz bands in each audio stream
  --gaps    video packet count against duration x frame rate, and the largest gap between packets
  --window  mean luma of the frame in the middle of T0-T1 (seconds), and mean volume of each audio stream in it

A FILE that ffprobe cannot read prints ffprobe's error to stderr and makes the exit status non-zero.
"""
import argparse
import json
import re
import subprocess
import sys


def run(*cmd):
    return subprocess.run(cmd, capture_output=True)


def color_name(rgb):
    names = {(1, 0, 0): "red", (0, 1, 0): "green", (0, 0, 1): "blue", (1, 1, 0): "yellow",
             (0, 0, 0): "black", (1, 1, 1): "white"}
    return names.get(tuple(int(c > 128) for c in rgb), "other") + str(rgb)


def mean_volume(stderr):
    m = re.search(r"mean_volume: (-?[\d.]+|-inf) dB", stderr)
    return m.group(1) if m else "?"


def probe(path, opts):
    result = run("ffprobe", "-v", "error", "-show_chapters", "-show_entries",
                 "stream=codec_type,codec_name,width,height,avg_frame_rate:format=duration:format_tags",
                 "-of", "json", path)
    if result.returncode != 0:
        err = result.stderr.decode(errors="replace").strip()
        sys.stdout.flush()
        print(f"{path}: ffprobe failed: {err}", file=sys.stderr)
        return False
    info = json.loads(result.stdout or "{}")
    streams = info.get("streams", [])
    video = next((s for s in streams if s["codec_type"] == "video"), None)
    audio_count = sum(1 for s in streams if s["codec_type"] == "audio")
    fmt = info.get("format", {})
    duration = float(fmt.get("duration", 0))
    if not video:
        print(f"{path}: no video stream, audio={audio_count} dur={duration:.2f}s")
        return True
    w, h = video["width"], video["height"]
    brand = fmt.get("tags", {}).get("major_brand", "-")
    print(f"{path}: {video['codec_name']} {w}x{h} fps={video['avg_frame_rate']} dur={duration:.2f}s "
          f"audio={audio_count} brand={brand} chapters={len(info.get('chapters', []))}")

    if opts.colors:
        frame = run("ffmpeg", "-v", "error", "-ss", "1", "-i", path, "-frames:v", "1",
                    "-f", "rawvideo", "-pix_fmt", "rgb24", "-")
        raw = frame.stdout
        if len(raw) >= w * h * 3:
            points = [(w // 4, h // 4), (3 * w // 4, h // 4), (w // 4, 3 * h // 4), (3 * w // 4, 3 * h // 4)]
            cols = [color_name(tuple(raw[(y * w + x) * 3:(y * w + x) * 3 + 3])) for x, y in points]
            print(f"   TL {cols[0]}  TR {cols[1]}  BL {cols[2]}  BR {cols[3]}")
        else:
            err = frame.stderr.decode(errors="replace").strip()
            print("   colors: no frame at 1 s" + (f": {err}" if err else ""))
    if opts.tones:
        for i in range(audio_count):
            levels = []
            for freq in (1000, 440):
                err = run("ffmpeg", "-v", "info", "-i", path, "-map", f"0:a:{i}", "-af",
                          f"bandpass=f={freq}:width_type=q:w=20,volumedetect", "-f", "null", "-").stderr
                levels.append(f"{freq}Hz={mean_volume(err.decode(errors='replace'))} dB")
            print(f"   a:{i} " + "  ".join(levels))
    if opts.gaps:
        out = run("ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "packet=pts_time",
                  "-of", "default=nw=1:nk=1", path).stdout.decode().split()
        times = sorted(float(t) for t in out if t not in ("", "N/A"))
        num, _, den = video["avg_frame_rate"].partition("/")
        fps = float(num) / float(den or 1) if float(den or 1) else 0
        gap = max((b - a for a, b in zip(times, times[1:])), default=0)
        print(f"   packets={len(times)} expected={duration * fps:.0f} maxgap={gap:.3f}s")
    for window in opts.window or []:
        t0, t1 = (float(t) for t in window.split("-"))
        err = run("ffmpeg", "-v", "info", "-ss", str((t0 + t1) / 2), "-i", path, "-frames:v", "1", "-vf",
                  "signalstats,metadata=print:key=lavfi.signalstats.YAVG", "-f", "null", "-").stderr
        m = re.search(r"YAVG=([\d.]+)", err.decode(errors="replace"))
        line = f"   {window}s: Y={float(m.group(1)):.0f}" if m else f"   {window}s: Y=?"
        for i in range(audio_count):
            err = run("ffmpeg", "-v", "info", "-ss", str(t0), "-t", str(t1 - t0), "-i", path, "-map", f"0:a:{i}",
                      "-af", "volumedetect", "-f", "null", "-").stderr
            line += f"  a{i}={mean_volume(err.decode(errors='replace'))}dB"
        print(line)
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--colors", action="store_true")
    parser.add_argument("--tones", action="store_true")
    parser.add_argument("--gaps", action="store_true")
    parser.add_argument("--window", action="append")
    parser.add_argument("files", nargs="+")
    opts = parser.parse_args()
    ok = True
    for path in opts.files:
        ok = probe(path, opts) and ok
    if not ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
