"""Generate the regression test media with ffmpeg.

usage: media.py <dir> [--h264 ENCODER]

Writes quad.png (1280x720: red, green / blue, yellow quadrants), quad-small.png (640x360),
quad-1k.mp4 (quad.png as 30 fps H.264 with a 1 kHz sine in AAC, 10 s), and tone440.wav
(440 Hz sine, 10 s). ENCODER defaults to libx264; pass one that `ffmpeg -encoders` lists.
"""
import argparse
import os
import subprocess
import sys


def ffmpeg(*args):
    subprocess.run(["ffmpeg", "-v", "error", "-y", *args], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dir")
    parser.add_argument("--h264", default="libx264")
    opts = parser.parse_args()

    os.makedirs(opts.dir, exist_ok=True)
    quad = os.path.join(opts.dir, "quad.png")
    colors = ("red", "lime", "blue", "yellow")
    inputs = [arg for color in colors for arg in ("-f", "lavfi", "-i", f"color={color}:s=640x360")]
    ffmpeg(*inputs, "-filter_complex", "[0][1]hstack[t];[2][3]hstack[b];[t][b]vstack", "-frames:v", "1", quad)
    ffmpeg("-i", quad, "-vf", "scale=640:360", os.path.join(opts.dir, "quad-small.png"))
    ffmpeg("-loop", "1", "-framerate", "30", "-i", quad,
           "-f", "lavfi", "-i", "sine=frequency=1000:sample_rate=48000",
           "-t", "10", "-c:v", opts.h264, "-pix_fmt", "yuv420p", "-c:a", "aac",
           os.path.join(opts.dir, "quad-1k.mp4"))
    ffmpeg("-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "10",
           os.path.join(opts.dir, "tone440.wav"))
    print("media written to", opts.dir)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        sys.exit(f"ffmpeg failed: {e}")
