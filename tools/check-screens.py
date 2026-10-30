#!/usr/bin/env python3
"""Render the app's tool tabs headless with `--screenshot`, and fail when one
does not render, renders blank, or renders the same as another tab.

    ./tools/check-screens.py            (ORION_SAMPLES names a folder of RAWs)

⚠ **Why this exists.** The SwiftUI app's version ran the `--screenshot` scenes
that assert (#125, #177): `--screenshot` had no gate at all before it, and a
check nobody runs is a comment. The Swift app was retired (decision #380) and
this is its port to the Avalonia app, whose `--screenshot` renders the real
window through Avalonia's headless platform — no window server, no window.

What each check is for, so a red says what broke:

  renders      every tab's frame is written within TIMEOUT. ⚠ Deleting the
               `--screenshot` dispatch in Program.cs does not make the app exit
               nonzero: with a photo given it **opens a real window and waits**,
               and this timeout is the only thing that ends it.
  not blank    the canvas region of each frame varies (a photograph is not one
               colour); a render that lost the picture is caught here
  tabs differ  the tool column differs between every pair of tabs: a `--tab`
               that stops switching renders Light seven times
  stable       Light rendered twice is byte-identical. A posing frame's only
               oracle is another frame, and one that disagrees with itself (a
               clock, a random id, an unsettled layout) has ruled that out.

⚠ **What this does NOT check**, said plainly:

  - what the Swift version's scenes asserted and the selftest now owns: the
    capture line draws the open photo's capture and no words for none
    (`SelfTest.Layout.cs` CaptureChecks); the footer DRAWS a failed render and
    a failed write in the warning colour, and stops after
    (`SelfTest.Sidecar.cs`, a real StatusPanel, not the model); the tool column
    scrolls past its fold (`SelfTest.Layout.cs`).
  - the menu bar. The Swift `menu` scene read `NSApp.mainMenu`; this app has no
    menu bar, its commands are keys and controls, and the selftest's
    CommandChecks hold every name a control binds to registered, and the
    registered names to a frozen list, so a key-only command deleted fails.
  - that any tab draws the *right* controls. A wrong panel differs from the
    other tabs and is stable, and passes.
"""

import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ORION = ROOT / "shell" / "bin" / "Release" / "net10.0" / "Orion"
SAMPLES = Path(os.environ.get("ORION_SAMPLES", ROOT / "samples"))
os.environ.setdefault("DOTNET_ROOT", "/opt/homebrew/opt/dotnet/libexec")

PHOTO_CANDIDATES = ["_PIC8220.ARW", "overexposed-background.ARW", "underexposedsubject.ARW", "_PIC8148.ARW"]
PHOTO = next((SAMPLES / c for c in PHOTO_CANDIDATES if (SAMPLES / c).is_file()), SAMPLES / PHOTO_CANDIDATES[0])

TABS = ["Light", "Color", "Detail", "Optics", "Mask", "Crop", "Presets"]

# Measured 2026-10-29: 2.1 s a frame. Generous, and well below anyone's
# patience: a deleted dispatch is reported by this timeout, once per tab.
TIMEOUT = 60


def decode_png(data: bytes):
    """Width, height and RGBA rows of an 8-bit RGBA, non-interlaced PNG."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, colour = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and colour == 6, f"PNG depth {depth} colour {colour}, expected 8-bit RGBA"
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * 4, [], bytearray(w * 4)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b, c = prev[i], prev[i - 4] if i >= 4 else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, rows


def region(png, x0, y0, x1, y1, step=8):
    """Sampled grey values of a box given in fractions of the frame."""
    w, h, rows = png
    return [sum(rows[y][x * 4:x * 4 + 3]) // 3
            for y in range(int(y0 * h), int(y1 * h), step) for x in range(int(x0 * w), int(x1 * w), step)]


def main():
    if not ORION.is_file():
        print(f"check-screens: no app at {ORION}\n  Build first: shell/native/build.sh && dotnet build -c Release shell",
              file=sys.stderr)
        return 2
    if not PHOTO.is_file():
        print(f"check-screens: no sample photograph at {PHOTO} (set ORION_SAMPLES)", file=sys.stderr)
        return 2

    problems, frames = [], {}
    with tempfile.TemporaryDirectory() as tmp:
        def shoot(tab, name):
            out = os.path.join(tmp, f"{name}.png")
            try:
                r = subprocess.run([str(ORION), str(PHOTO), "--tab", tab, "--screenshot", out],
                                   capture_output=True, text=True, timeout=TIMEOUT)
            except subprocess.TimeoutExpired:
                problems.append(f"{tab} did not finish in {TIMEOUT}s — it is likely waiting in a window rather than "
                                f"rendering one. Check that Program.cs still runs --screenshot headless")
                return None
            if r.returncode != 0 or not os.path.isfile(out):
                problems.append(f"{tab} exited {r.returncode} without a frame\n      {(r.stderr or r.stdout).strip()[-400:]}")
                return None
            return Path(out).read_bytes()

        for tab in TABS:
            data = shoot(tab, tab)
            if data is None:
                continue
            png = decode_png(data)
            canvas = region(png, 0.05, 0.15, 0.70, 0.80)
            mean = sum(canvas) / len(canvas)
            spread = (sum((v - mean) ** 2 for v in canvas) / len(canvas)) ** 0.5
            if spread < 8:
                problems.append(f"{tab}: the canvas is flat (grey deviation {spread:.1f}) — the picture is not drawn")
            frames[tab] = region(png, 0.78, 0.30, 1.0, 0.85, step=4)

        tabs = list(frames)
        for i, a in enumerate(tabs):
            for b in tabs[i + 1:]:
                if frames[a] == frames[b]:
                    problems.append(f"{a} and {b} draw the same tool column — --tab no longer switches the tab")

        first, second = shoot("Light", "light-1"), shoot("Light", "light-2")
        if first is not None and second is not None and first != second:
            problems.append(f"Light is not byte-stable across two renders ({len(first)} against {len(second)} bytes): "
                            f"something in the frame is a clock, a random value or an unsettled layout")

    if problems:
        print(f"check-screens: {len(problems)} problem(s)\n")
        for p in problems:
            print(f"  {p}\n")
        return 1
    print(f"check-screens: {len(frames)} tab(s) rendered headless, none blank, all different; Light byte-stable.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
