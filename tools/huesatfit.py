#!/usr/bin/env python3
"""Fit — or check — the camera profile's per-hue saturation curve (#232).

    tools/huesatfit.py --fit [iterations]
    tools/huesatfit.py --check

Orion renders warm hues (orange/yellow) markedly less saturated than the
developer's camera, and red/magenta slightly more. `blueSky()` and
`warmTan()` in `HueSatMap.h` already fix HUE for two regions; this fits the
residual SATURATION gap directly into the table's own 90 hue bins.

Corpus: every third frame of the developer's own Sony shoot,
`~/Pictures/sept 5th forks` (78 uncompressed ARWs). No sidecars belong there
— `--batch-export` renders as-shot, which is the whole point: the reference
is free, because every RAW carries the camera's own JPEG inside it
(`exiftool -b -JpgFromRaw`, falling back to `-PreviewImage`).

Method: batch-export the corpus, extract each camera JPEG, downsample both
to 700px with `sips` and decode with a pure-python PNG reader (reproduces
`orion-pixstat`'s mean RGB exactly — see decision #231 for why `orion-
pixstat`'s own `sat` field is the wrong tool for a whole frame). Measure
mean per-pixel saturation, camera vs. orion, in the same seven hue bands
`hue.py` used to establish this problem in the first place — they are wide
enough (30-90 degrees) to be measured with low noise from a few dozen
frames, which a direct 90-bin measurement is NOT: an earlier version of
this file fit each of the 90 bins independently and it did not converge —
mean per-bin error *grew* round over round even as the worst single bin
improved, because a 4-degree bin's pixel count in any one hue is small
enough, and correlated enough (one frame's one flower dominates it), that
the "correction" was mostly chasing sampling noise.

The seven band scales become seven CONTROL POINTS, one at each band's
center, and `interp_curve` fills all 90 table bins by circular LINEAR
interpolation between them. This is not the 8-band `satShift` mixer that
was already tried and failed, and the difference is the shape, not the
count: a fixed-width falloff kernel (that mixer, and this table's own
`blueSky`/`warmTan` regions) never reaches a neighbor's full value if the
neighbor is closer than the kernel's own width, which is exactly what
happens between red and orange (30 degrees apart, inside `warmTan`'s own
45-degree half-width). Linear interpolation between two control points
reaches BOTH exactly, at their own centers, whatever the gap between them —
sharper by construction, not by a narrower magic number.

Fitting is still a feedback loop, not one inversion: each round renders
with the previous round's curve already applied (via `ORION_HUESAT_CURVE`),
remeasures the seven bands, and multiplies in `(1/ratio)^DAMPING` — damped
because band-level rounds still overshoot at full weight (orange crossed
1.0 and kept climbing under an earlier undamped run), the same reason
`blueSky`/`warmTan` were themselves fitted by sweeping rather than solving
in one step.

⚠ This is a camera-matching profile, not a correctness fix. Decision #229
found Orion already within 2% of Apple's own RAW pipeline on the same
frames; the gap this closes is to one camera's house style, deliberately,
the way Lightroom ships a profile per body.

Run from the repository root, after a build.
"""
import os
import shutil
import struct
import subprocess
import sys
import zlib

import numpy as np

ORION = "./build/Orion.app/Contents/MacOS/Orion"
SRC = os.path.expanduser("~/Pictures/sept 5th forks")
WORK = "/tmp/orion-huesatfit"

N_HUE = 90            # matches huesat::kHueDivisions — one bin per 4 degrees
STRIDE = 3            # every 3rd frame of the 78 -> ~26, spanning the shoot
MIN_PIXELS = 8000     # a band under this, in the whole corpus, is not fitted
CLAMP_LO, CLAMP_HI = 0.65, 1.35   # sanity bound on any control point — see writeup
DAMPING = 0.6         # <1 update gain per round — an undamped first run
                      # overshot (orange crossed 1.0 and kept climbing); see
                      # the module docstring
DOWNSAMPLE_PX = 700   # matches the rest of this investigation's rig

# name, [lo, hi) in degrees — same seven bands `hue.py` used to first
# measure this gap. Also the fit's seven control points, one per center.
BANDS = [("red", 345, 15), ("orange", 15, 45), ("yellow", 45, 75),
         ("green", 75, 165), ("cyan", 165, 195), ("blue", 195, 255),
         ("magenta", 255, 345)]


def _band_center(lo, hi):
    return ((lo + (hi if hi > lo else hi + 360.0)) / 2.0) % 360.0


BAND_CENTERS = [(name, _band_center(lo, hi)) for name, lo, hi in BANDS]

# The compiled-in fit, decision #232 — `tools/huesatfit.py --check` pins it.
# Kept here (not parsed from HueSatMap.h) so this file has no C++ dependency;
# `--fit` prints a ready-to-paste array when it converges, and the two are
# expected to be edited together. 12 rounds against the 26-frame corpus,
# DAMPING 0.6, CLAMP [0.65, 1.35] — research/camera-profiles.md has the
# convergence table and the per-band residual this left, including the one
# that did not fully close (red) and the diagnosed reason why.
FITTED = [
    0.650, 0.708, 0.766, 0.824, 0.882, 0.940, 0.997, 1.055, 1.102, 1.137,
    1.173, 1.208, 1.244, 1.279, 1.315, 1.350, 1.328, 1.306, 1.284, 1.263,
    1.241, 1.219, 1.197, 1.175, 1.153, 1.131, 1.110, 1.088, 1.066, 1.044,
    1.022, 1.044, 1.066, 1.088, 1.110, 1.131, 1.153, 1.175, 1.197, 1.219,
    1.241, 1.263, 1.284, 1.306, 1.328, 1.350, 1.311, 1.271, 1.232, 1.192,
    1.153, 1.114, 1.074, 1.035, 0.995, 0.956, 0.917, 0.897, 0.883, 0.869,
    0.855, 0.842, 0.828, 0.814, 0.801, 0.787, 0.773, 0.760, 0.746, 0.732,
    0.718, 0.705, 0.691, 0.677, 0.664, 0.650, 0.650, 0.650, 0.650, 0.650,
    0.650, 0.650, 0.650, 0.650, 0.650, 0.650, 0.650, 0.650, 0.650, 0.650,
]

# The fit's own worst-band residual after convergence was 0.196 (red — see
# research/camera-profiles.md for why that one band did not fully close),
# plus room for corpus/rounding noise across a re-run. Loose next to
# `blueSky`'s 0.02 or `warmTan`'s degree-tolerance by necessity — this is a
# harder fit (see the module docstring) — but still tight enough that a real
# regression (the curve reverting to identity, say) trips it immediately.
TOLERANCE = 0.24


def corpus():
    names = sorted(f[:-4] for f in os.listdir(SRC) if f.endswith(".ARW"))
    if not names:
        sys.exit(f"huesatfit: no ARWs found in {SRC!r}")
    return names[::STRIDE]


# ── PNG decode (validated against orion-pixstat's mean RGB) ────────────────

def read_png(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", f"not a png: {path}"
    pos, idat, w, h = 8, b"", None, None
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", body[:10])
            assert bd == 8 and ct in (2, 6), f"bitdepth {bd} colortype {ct}"
            nch = 3 if ct == 2 else 4
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * nch
    out = np.zeros((h, stride), dtype=np.uint8)
    prev = np.zeros(stride, dtype=np.uint8)
    i = 0
    for y in range(h):
        f = raw[i]; i += 1
        line = np.frombuffer(raw[i:i + stride], dtype=np.uint8).astype(np.int32)
        i += stride
        if f == 0:
            cur = line
        elif f == 1:
            cur = line.copy()
            for x in range(nch, stride):
                cur[x] = (cur[x] + cur[x - nch]) & 255
        elif f == 2:
            cur = (line + prev) & 255
        elif f == 3:
            cur = line.copy()
            for x in range(stride):
                a = cur[x - nch] if x >= nch else 0
                cur[x] = (cur[x] + ((a + int(prev[x])) >> 1)) & 255
        elif f == 4:
            cur = line.copy()
            for x in range(stride):
                a = int(cur[x - nch]) if x >= nch else 0
                b = int(prev[x])
                c = int(prev[x - nch]) if x >= nch else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                cur[x] = (cur[x] + pr) & 255
        else:
            raise SystemExit(f"filter {f}")
        cur = cur.astype(np.uint8)
        out[y] = cur
        prev = cur
    return out.reshape(h, w, nch)[:, :, :3].astype(np.float64) / 255.0


def hue_sat(png_path):
    """Per-pixel hue in degrees and saturation, lit and non-gray only."""
    a = read_png(png_path)
    mx = a.max(axis=2); mn = a.min(axis=2)
    sat = np.where(mx > 1e-6, (mx - mn) / np.maximum(mx, 1e-6), 0.0)
    luma = 0.2126 * a[:, :, 0] + 0.7152 * a[:, :, 1] + 0.0722 * a[:, :, 2]
    r, g, b = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    hue = np.degrees(np.arctan2(np.sqrt(3) * (g - b), 2 * r - g - b)) % 360.0
    mask = (luma > 0.05) & (sat > 0.05)
    return hue[mask], sat[mask]


def hue_bin(hue_deg):
    return np.minimum((hue_deg // (360.0 / N_HUE)).astype(int), N_HUE - 1)


def interp_curve(values):
    """`values`: band name -> scale. Circular piecewise-linear interpolation
    across all `N_HUE` table bins between the seven `BAND_CENTERS` — reaches
    each band's own value exactly at its center; see the module docstring
    for why this, not a falloff kernel, is what makes red and orange (30
    degrees apart) independently correctable."""
    centers = np.array([c for _, c in BAND_CENTERS])
    vals = np.array([values[name] for name, _ in BAND_CENTERS])
    order = np.argsort(centers)
    c, v = centers[order], vals[order]
    c_ext = np.concatenate([c - 360.0, c, c + 360.0])
    v_ext = np.concatenate([v, v, v])
    hues = np.arange(N_HUE) * (360.0 / N_HUE)
    return np.clip(np.interp(hues, c_ext, v_ext), CLAMP_LO, CLAMP_HI)


# ── Render + extract ────────────────────────────────────────────────────────

def render_corpus(names, curve=None):
    """Batch-export each frame (optionally with a candidate curve), extract
    its camera JPEG, downsample both to a 700px PNG. Camera side is cached
    on disk across calls — it never changes. Returns name -> (cam_png, ori_png)."""
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ)
    if curve is not None:
        env["ORION_HUESAT_CURVE"] = ",".join(f"{v:.5f}" for v in curve)
    else:
        env.pop("ORION_HUESAT_CURVE", None)

    out = {}
    for n in names:
        raw = os.path.join(SRC, n + ".ARW")
        cam_png = os.path.join(WORK, n + ".cam.png")
        if not os.path.exists(cam_png):
            cam_jpg = os.path.join(WORK, n + ".cam.jpg")
            with open(cam_jpg, "wb") as fh:
                subprocess.run(["exiftool", "-b", "-JpgFromRaw", raw],
                                stdout=fh, stderr=subprocess.DEVNULL)
            if os.path.getsize(cam_jpg) == 0:
                with open(cam_jpg, "wb") as fh:
                    subprocess.run(["exiftool", "-b", "-PreviewImage", raw],
                                    stdout=fh, stderr=subprocess.DEVNULL)
            if os.path.getsize(cam_jpg) == 0:
                sys.exit(f"huesatfit: {n} carries no embedded JPEG")
            subprocess.run(["sips", "-Z", str(DOWNSAMPLE_PX), "-s", "format", "png",
                             cam_jpg, "--out", cam_png],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        # A fresh directory every render: BatchExport's own collision rule
        # renames rather than overwrites, so reusing a directory across
        # rounds would silently keep reading the FIRST round's file forever.
        exp_dir = os.path.join(WORK, "exp_" + n)
        shutil.rmtree(exp_dir, ignore_errors=True)
        r = subprocess.run([ORION, "--batch-export", exp_dir, raw],
                            env=env, capture_output=True, text=True)
        ori_jpg = os.path.join(exp_dir, n + ".jpg")
        if not os.path.exists(ori_jpg):
            sys.exit(f"huesatfit: export failed for {n}\n{r.stderr}")
        ori_png = os.path.join(WORK, n + ".ori.png")
        subprocess.run(["sips", "-Z", str(DOWNSAMPLE_PX), "-s", "format", "png",
                         ori_jpg, "--out", ori_png],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        out[n] = (cam_png, ori_png)
    return out


# ── Measurement ──────────────────────────────────────────────────────────

def aggregate(files):
    """Pixel-weighted per-90-bin mean saturation, camera and orion, over the
    whole corpus. Returns (cam_mean[90], cam_n[90], ori_mean[90], ori_n[90])."""
    cam_sum = np.zeros(N_HUE); cam_n = np.zeros(N_HUE)
    ori_sum = np.zeros(N_HUE); ori_n = np.zeros(N_HUE)
    for cam_png, ori_png in files.values():
        ch, cs = hue_sat(cam_png)
        oh, os_ = hue_sat(ori_png)
        cb, ob = hue_bin(ch), hue_bin(oh)
        np.add.at(cam_sum, cb, cs); np.add.at(cam_n, cb, 1)
        np.add.at(ori_sum, ob, os_); np.add.at(ori_n, ob, 1)
    cam_mean = np.divide(cam_sum, cam_n, out=np.full(N_HUE, np.nan), where=cam_n > 0)
    ori_mean = np.divide(ori_sum, ori_n, out=np.full(N_HUE, np.nan), where=ori_n > 0)
    return cam_mean, cam_n, ori_mean, ori_n


def band_report(cam_mean, ori_mean, cam_n, ori_n):
    rows = []
    deg = 360.0 / N_HUE
    for name, lo, hi in BANDS:
        if lo < hi:
            sel = [h for h in range(N_HUE) if lo <= h * deg < hi]
        else:
            sel = [h for h in range(N_HUE) if h * deg >= lo or h * deg < hi]
        cn = sum(cam_n[h] for h in sel); on = sum(ori_n[h] for h in sel)
        if cn < MIN_PIXELS or on < MIN_PIXELS:
            rows.append((name, None)); continue
        c = sum(cam_mean[h] * cam_n[h] for h in sel if not np.isnan(cam_mean[h])) / cn
        o = sum(ori_mean[h] * ori_n[h] for h in sel if not np.isnan(ori_mean[h])) / on
        rows.append((name, o / c))
    return rows


def print_bands(rows, label):
    print(f"  {label:<10}" + "".join(
        f"  {n[:3]}={'  n/a' if r is None else f'{r:5.3f}'}" for n, r in rows))


# ── Fit ──────────────────────────────────────────────────────────────────

def fit(iterations):
    names = corpus()
    print(f"corpus: {len(names)} frames (every {STRIDE}th of {len(os.listdir(SRC))}), "
          f"downsampled {DOWNSAMPLE_PX}px")

    state = {name: 1.0 for name, _, _ in BANDS}
    for it in range(iterations):
        curve = interp_curve(state)
        files = render_corpus(names, curve=curve if it > 0 else None)
        cam_mean, cam_n, ori_mean, ori_n = aggregate(files)
        rows = band_report(cam_mean, ori_mean, cam_n, ori_n)

        print(f"\niteration {it}: {'starting curve (compiled)' if it == 0 else 'residual after previous round'}")
        print_bands(rows, "orion/cam")
        print_bands([(n, state[n]) for n, _, _ in BANDS], "curve now")

        for name, ratio in rows:
            if ratio is None:
                continue
            target = (1.0 / ratio) ** DAMPING
            state[name] = float(np.clip(state[name] * target, CLAMP_LO, CLAMP_HI))

    # One more render+measure at the converged curve, to report the actual
    # residual rather than the correction that produced it.
    curve = interp_curve(state)
    files = render_corpus(names, curve=curve)
    cam_mean, cam_n, ori_mean, ori_n = aggregate(files)
    rows = band_report(cam_mean, ori_mean, cam_n, ori_n)
    print(f"\nconverged, {iterations} rounds:")
    print_bands(rows, "orion/cam")
    worst = max((abs(r - 1.0) for _, r in rows if r is not None), default=0.0)
    print(f"  worst band residual: {worst:.3f}")

    print("\ncontrol points (name=center=scale):")
    print("  " + "  ".join(f"{n}={c:.0f}={state[n]:.3f}" for n, c in BAND_CENTERS))
    print("\nkFitted array (paste into HueSatMap.h and FITTED above):")
    for i in range(0, N_HUE, 10):
        print("        " + ", ".join(f"{v:.3f}f" for v in curve[i:i + 10]) + ",")
    return 0


def check():
    names = corpus()
    files = render_corpus(names, curve=None)   # compiled-in curve, no override
    cam_mean, cam_n, ori_mean, ori_n = aggregate(files)
    rows = band_report(cam_mean, ori_mean, cam_n, ori_n)
    print(f"corpus: {len(names)} frames")
    print_bands(rows, "orion/cam")
    worst = max((abs(r - 1.0) for _, r in rows if r is not None), default=0.0)
    print(f"\nworst band residual {worst:.3f}   tolerance {TOLERANCE}")
    if worst > TOLERANCE:
        print("\nFAIL: the compiled curve has drifted from the fitted target.")
        print("Re-fit with huesatfit.py --fit, or find what changed upstream —")
        print("this measures the whole pipeline, not the table in isolation.")
        return 1
    print("\nok")
    return 0


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--check":
        sys.exit(check())
    if len(sys.argv) in (2, 3) and sys.argv[1] == "--fit":
        sys.exit(fit(int(sys.argv[2]) if len(sys.argv) == 3 else 3))
    sys.exit(__doc__)
