#!/usr/bin/env python3
"""Which parts of the real screen change while a scene runs on X11 (under Xvfb).

PlaySuite's frame captures repaint the whole window, so they can never show a pixel the
host failed to update, or one it changed without reason. Under Xvfb the screen itself can
be read: this runs a scene, grabs the root window with `xwd` several times, and reports,
for each pair of grabs, the regions that changed, in window points. A resting Gems board
should change only inside the board; a still card table only where the pointer is; and a
pixel outside the frames' damage must never change (GUI.Forms' DP gate). Pure Python and
the X tools only (xwd, xdotool, Xvfb), so it runs in the bare Lima guest.

    python3 tools/x11_screen_check.py --app ~/build/playsuite/games --scene gems
    python3 tools/x11_screen_check.py --app ... --scene solitaire --hover 120,400 --grabs 3
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def read_xwd(path: Path) -> tuple[int, int, int, bytes]:
    """Width, height, bytes per row and the pixels of a 24/32-bit ZPixmap xwd dump."""
    data = path.read_bytes()
    header = struct.unpack(">25I", data[:100])
    header_size, width, height = header[0], header[4], header[5]
    bits, row = header[11], header[12]
    colours = header[19]
    if bits != 32:
        raise RuntimeError(f"{path}: {bits} bits per pixel; run Xvfb with a 24-bit screen")
    start = header_size + colours * 12
    return width, height, row, data[start:start + row * height]


def first_difference(a: bytes, b: bytes, step: int = 256) -> int:
    for offset in range(0, len(a), step):
        if a[offset:offset + step] != b[offset:offset + step]:
            for i in range(offset, min(len(a), offset + step)):
                if a[i] != b[i]:
                    return i
    return -1


def last_difference(a: bytes, b: bytes, step: int = 256) -> int:
    end = len(a)
    while end > 0:
        offset = max(0, end - step)
        if a[offset:end] != b[offset:end]:
            for i in range(end - 1, offset - 1, -1):
                if a[i] != b[i]:
                    return i
        end = offset
    return -1


def changed_regions(first: bytes, second: bytes, width: int, height: int, row: int) -> list[list[int]]:
    """Boxes [x0, y0, x1, y1] (pixels) covering what changed: rows grouped while they touch."""
    boxes: list[list[int]] = []
    for y in range(height):
        a = first[y * row:y * row + width * 4]
        b = second[y * row:y * row + width * 4]
        if a == b:
            continue
        x0, x1 = first_difference(a, b) // 4, last_difference(a, b) // 4 + 1
        last = boxes[-1] if boxes else None
        if last is not None and last[3] == y and x0 <= last[2] and x1 >= last[0]:
            last[0], last[2], last[3] = min(last[0], x0), max(last[2], x1), y + 1
        else:
            boxes.append([x0, y, x1, y + 1])
    return boxes


def window_origin(display: str) -> tuple[int, int]:
    found = subprocess.run(["xdotool", "search", "--name", "PlaySuite"], env=dict(os.environ, DISPLAY=display),
                           capture_output=True, text=True)
    ids = found.stdout.split()
    if not ids:
        return 0, 0
    geometry = subprocess.run(["xdotool", "getwindowgeometry", "--shell", ids[-1]],
                              env=dict(os.environ, DISPLAY=display), capture_output=True, text=True).stdout
    values = dict(line.split("=", 1) for line in geometry.splitlines() if "=" in line)
    return int(values.get("X", 0)), int(values.get("Y", 0))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", type=Path, required=True, help="the native development build's executable")
    parser.add_argument("--scene", required=True, help="the game to open (its id, as --list-games prints it)")
    parser.add_argument("--settle", type=float, default=8.0, help="seconds after opening before the first grab")
    parser.add_argument("--grabs", type=int, default=4)
    parser.add_argument("--interval", type=float, default=1.0, help="seconds between grabs")
    parser.add_argument("--hover", help="X,Y in window points: the pointer goes there after the first grab")
    parser.add_argument("--display", default=":99")
    options = parser.parse_args()
    for tool in ("xwd", "xdotool"):
        if shutil.which(tool) is None:
            print(f"needs {tool}", file=sys.stderr)
            return 2
    environment = dict(os.environ, DISPLAY=options.display)
    if subprocess.run(["xdotool", "getmouselocation"], env=environment, capture_output=True).returncode != 0:
        subprocess.Popen(["Xvfb", options.display, "-screen", "0", "1920x1080x24"], stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)
        time.sleep(2)
    total = options.settle + options.grabs * options.interval + 5
    with tempfile.TemporaryDirectory() as work:
        script = Path(work) / "scene.txt"
        script.write_text(f"300 resize 1100 720\n1000 open {options.scene}\n{int(1000 + total * 1000)} quit\n",
                          encoding="utf-8")
        state = Path(work) / "state"
        state.mkdir()
        app = subprocess.Popen([str(options.app), "--dev", "--script", str(script)],
                               env=dict(environment, GAMES_STATE_DIR=str(state)),
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1 + options.settle)
        left, top = window_origin(options.display)
        grabs = []
        for k in range(options.grabs):
            path = Path(work) / f"grab{k}.xwd"
            subprocess.run(["xwd", "-root", "-silent", "-out", str(path)], env=environment, check=True)
            grabs.append(read_xwd(path))
            if k == 0 and options.hover:
                # the one known change, between the first grab and the second
                x, y = (int(v) for v in options.hover.split(","))
                subprocess.run(["xdotool", "mousemove", str(left + x), str(top + y)], env=environment)
            time.sleep(options.interval)
        app.wait(timeout=total + 30)
    report = {"scene": options.scene, "window_origin": [left, top], "pairs": []}
    for k in range(1, len(grabs)):
        width, height, row, before = grabs[k - 1]
        after = grabs[k][3]
        boxes = changed_regions(before, after, width, height, row)
        report["pairs"].append([[x0 - left, y0 - top, x1 - left, y1 - top] for x0, y0, x1, y1 in boxes])
    print(json.dumps(report))
    for k, boxes in enumerate(report["pairs"], 1):
        area = sum((b[2] - b[0]) * (b[3] - b[1]) for b in boxes)
        bounds = [min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes),
                  max(b[3] for b in boxes)] if boxes else None
        print(f"grabs {k - 1}->{k}: {len(boxes)} changed regions, {area} px, within {bounds}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
