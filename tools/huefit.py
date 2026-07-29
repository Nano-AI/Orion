#!/usr/bin/env python3
"""Fit — or check — the camera profile's blue correction against real skies.

    tools/huefit.py --check
    tools/huefit.py 250 60 "-6,-8,-10" "1.0,1.05,1.10"

`--check` renders the reference frame with the constants that are compiled in
and fails if the sky has drifted from the fitted target. That is the pin the
node's own GPU test cannot provide: the test proves the node does what the
table says, and this proves the table is still the right table.

It lives here rather than in `apps/tests` because it needs `samples/`, which is
local-only — so it is a command the developer runs, not a suite that runs
everywhere. Everything that *can* be machine-independent is in orion-tests
(`testHueSatMapGpu`): matrix round trip, spec table structure, neutral in ->
neutral out, blue moves and other hues do not.

Targets are the mean of two independent renderings of the same frame, measured
with orion-pixstat: the camera's own JPEG and Apple's RAW pipeline. They agree
to within 0.017 in R/B, which is why the mean is worth aiming at.

    patch                sony R/B   apple R/B    sony G/B   apple G/B
    sky upper (saturated)   0.458       0.441       0.713      0.671
    sky lower (hazy)        0.651       0.643       0.838      0.806

Run from the repository root, after a build.
"""
import os
import subprocess
import sys

ORION = "./build/Orion.app/Contents/MacOS/Orion"
RAW = "samples/_PIC8095.ARW"
SHOT = "/tmp/orion-huefit.png"

# patch, target R/B, target G/B. None means "must not move" — a correction that
# has spread outside blue shows up here before anyone sees it in a picture.
PATCHES = [
    ("0.20,0.05,0.15,0.08", 0.4495, 0.692),
    ("0.10,0.45,0.10,0.06", 0.6470, 0.822),
    ("0.30,0.88,0.08,0.04", None, None),      # foliage
    ("0.60,0.77,0.09,0.03", None, None),      # white sign
]

# The fit's own error, plus room for a driver's rounding. Tighter than the
# spread between the two references, so a real drift trips it first.
TOLERANCE = 0.02


def render(cfg=None):
    cmd = [ORION, "--screenshot", SHOT, "--photo", RAW, "--scene", "asshot"]
    for p, _, _ in PATCHES:
        cmd += ["--measure", p]
    env = dict(os.environ)
    if cfg:
        env["ORION_HUESAT"] = cfg
    else:
        env.pop("ORION_HUESAT", None)
    out = subprocess.run(cmd, capture_output=True, text=True, env=env).stderr
    means, cur = [], {}
    for line in out.splitlines():
        s = line.split()
        if len(s) >= 4 and s[0] in ("R", "G", "B") and s[1] == "mean":
            cur[s[0]] = float(s[2])
            if len(cur) == 3:
                means.append((cur["R"], cur["G"], cur["B"]))
                cur = {}
    if len(means) != len(PATCHES):
        sys.exit(f"huefit: expected {len(PATCHES)} measurements, got {len(means)}. "
                 f"Is {RAW} present and the app built?")
    return [(r / b, g / b) for r, g, b in means]


def error(ratios):
    total, n = 0.0, 0
    for (rb, gb), (_, trb, tgb) in zip(ratios, PATCHES):
        if trb is None:
            continue
        total += abs(rb - trb) + abs(gb - tgb)
        n += 2
    return total / n


def check():
    ratios = render()
    err = error(ratios)
    print(f"sky error {err:.4f}   tolerance {TOLERANCE}")
    for (rb, gb), (p, trb, tgb) in zip(ratios, PATCHES):
        want = f"want {trb:.3f}/{tgb:.3f}" if trb else "must not move"
        print(f"  {p:<22} {rb:.3f}/{gb:.3f}   {want}")
    if err > TOLERANCE:
        print("\nFAIL: the rendered sky has drifted from the fitted target.")
        print("Re-fit with huefit.py <centre> <width> <shifts> <scales>, or find")
        print("what changed upstream — this measures the whole pipeline, not the node.")
        return 1
    print("\nok")
    return 0


def sweep(centre, width, shifts, scales):
    base = render(f"{centre},{width},0,1.0")
    print("identity table: " + "  ".join(f"{rb:.3f}/{gb:.3f}" for rb, gb in base))
    print(f"centre {centre}  width {width}")
    print("shift " + "".join(f"     sat={s:<4}" for s in scales))
    best = None
    for sh in shifts:
        row = f"{sh:+5.1f}"
        for sc in scales:
            r = render(f"{centre},{width},{sh},{sc}")
            e = error(r)
            row += f"  {e:.4f}({r[0][0]:.2f},{r[1][0]:.2f})"
            if best is None or e < best[0]:
                best = (e, sh, sc, r)
        print(row)
    e, sh, sc, r = best
    print(f"-> best shift {sh:+.1f}  sat {sc:.2f}   err {e:.4f}")
    for (rb, gb), (p, trb, tgb) in zip(r, PATCHES):
        want = f"want {trb:.3f}/{tgb:.3f}" if trb else "must not move"
        print(f"   {p:<22} {rb:.3f}/{gb:.3f}   {want}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--check":
        sys.exit(check())
    if len(sys.argv) == 5:
        sys.exit(sweep(sys.argv[1], sys.argv[2],
                       [float(x) for x in sys.argv[3].split(",")],
                       [float(x) for x in sys.argv[4].split(",")]))
    sys.exit(__doc__)
