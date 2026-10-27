#!/usr/bin/env python3
"""Swift-versus-Avalonia screenshots for the slice swarm (#315).

    compare.py check             every Swift UI file has exactly one owner in slices.json
    compare.py swift [SLICE]     Orion.app --screenshot per scene    -> reference/<scene>.png
    compare.py avalonia [SLICE]  OrionShell --panel per panel        -> compare/<panel>.png
    compare.py page              compare/index.html, each slice's panels beside its scenes

Serial on purpose: one GPU, 24 GB. The sample is the first that exists of
$ORION_SAMPLE, samples/_PIC8095.ARW here or in the main checkout, and any worktree's copy.
"""
import html
import json
import os
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SLICES = json.loads((HERE / "slices.json").read_text())["slices"]
MANIFEST = json.loads((HERE / "slices.json").read_text())
ORION = ROOT / "build/Orion.app/Contents/MacOS/Orion"
SHELL = HERE / "bin/Release/net10.0/OrionShell"
REFERENCE, COMPARE = HERE / "reference", HERE / "compare"
# The apphost exits 131 without it; Homebrew does not set it.
os.environ.setdefault("DOTNET_ROOT", "/opt/homebrew/opt/dotnet/libexec")


def sample():
    # From a worktree, the main checkout is where the samples and worktrees are.
    common = subprocess.run(["git", "-C", HERE, "rev-parse", "--path-format=absolute", "--git-common-dir"],
                            capture_output=True, text=True).stdout.strip()
    main = pathlib.Path(common).parent if common else ROOT
    candidates = [os.environ.get("ORION_SAMPLE"), ROOT / "samples/_PIC8095.ARW", main / "samples/_PIC8095.ARW",
                  *sorted((main / ".claude/worktrees").glob("*/samples/_PIC8095.ARW"))]
    for c in candidates:
        if c and pathlib.Path(c).is_file():
            return str(c)
    sys.exit("compare: no sample RAW; set ORION_SAMPLE")


def chosen(name):
    return {name: SLICES[name]} if name else SLICES


def run(cmd):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=180)
    ok = r.returncode == 0
    print(("ok   " if ok else "FAIL ") + " ".join(str(c) for c in cmd[1:4]) + ("" if ok else "\n" + r.stderr[-800:]))
    return ok


def check():
    """Every UI file (imports SwiftUI/AppKit/MetalKit) is owned once."""
    ui = {p.name for p in (ROOT / "app").glob("*.swift")
          if re.search(r"^import (SwiftUI|AppKit|MetalKit)", p.read_text(), re.M)}
    owners = {}
    def claim(entry, owner):
        pattern = ".*".join(re.escape(part) for part in entry.split(" ")[0].split("*"))
        for f in ui:
            if re.fullmatch(pattern, f):
                # "X.swift (the .crop tab only)" claims a section: several may share a file.
                owners.setdefault(f, []).append((owner, " (" in entry))
    for s, spec in SLICES.items():
        for e in spec["swift"]:
            claim(e, s)
    for e in MANIFEST["shared"]:
        claim(e, "shared")
    for e in MANIFEST["not_ported"]:
        claim(e, "not_ported")
    orphans = sorted(ui - owners.keys())
    # Owned twice: a whole-file claim beside any other claim. Section claims may share.
    doubled = sorted(f for f, o in owners.items() if len(o) > 1 and any(not partial for _, partial in o))
    print(f"check: {len(ui)} Swift UI files, {len(orphans)} unowned, {len(doubled)} owned twice")
    for f in orphans:
        print("  unowned:", f)
    for f in doubled:
        print("  owned twice:", f, owners[f])
    return not orphans and not doubled


def swift(name):
    REFERENCE.mkdir(exist_ok=True)
    photo = sample()
    return all([run([ORION, "--screenshot", REFERENCE / f"{scene}.png", "--photo", photo, "--scene", scene])
                for spec in chosen(name).values() for scene in spec["scenes"]])


def avalonia(name):
    COMPARE.mkdir(exist_ok=True)
    photo = sample()
    wanted = {p["name"] for spec in chosen(name).values() for p in spec["panels"]}
    listed = subprocess.run([SHELL, "--list"], capture_output=True, text=True, check=True).stdout
    registered = [line.split("\t")[0] for line in listed.splitlines() if line]
    panels = [p for p in registered if not name or p in wanted]
    if name:
        print(f"avalonia: {len(panels)} of {len(wanted)} {name} panels registered")
    ok = all([run([SHELL, photo, "--panel", p, "--screenshot", COMPARE / f"{p}.png"]) for p in panels])
    if not name:
        ok &= all([run([SHELL, photo, "--tab", t, "--screenshot", COMPARE / f"window-{t}.png"])
                   for t in ["Light", "Color", "Detail", "Optics", "Mask", "Crop", "Presets"]])
    return ok


def page():
    """One offline page: per slice, its panels (or what is missing) beside the Swift scenes."""
    def img(path, caption):
        rel = os.path.relpath(path, COMPARE)
        return (f'<figure><img src="{html.escape(rel)}" loading="lazy">'
                f'<figcaption>{html.escape(caption)}</figcaption></figure>')
    out = ["<!doctype html><meta charset=utf-8><title>Avalonia vs Swift</title>",
           "<style>body{background:#141416;color:#E8E8EA;font:13px -apple-system,system-ui,sans-serif;margin:24px}"
           "h2{margin:32px 0 8px}.row{display:flex;gap:12px;overflow-x:auto;align-items:flex-start}"
           "figure{margin:0}img{max-height:520px;border:1px solid #313135}figcaption{color:#8A8A90;margin:4px 0}"
           ".missing{color:#C4574D}</style>",
           "<h1>Avalonia shell vs Swift (#315)</h1>"]
    windows = sorted(COMPARE.glob("window-*.png"))
    if windows:
        out += ["<h2>Whole window</h2><div class=row>", *[img(w, w.stem) for w in windows], "</div>"]
    for s, spec in SLICES.items():
        out.append(f"<h2>{html.escape(s)}</h2><div class=row>")
        for p in spec["panels"]:
            shot = COMPARE / f"{p['name']}.png"
            out.append(img(shot, p["name"]) if shot.exists()
                       else f'<p class=missing>{html.escape(p["name"])}: not registered yet</p>')
        out.append("</div><div class=row>")
        out += [img(REFERENCE / f"{sc}.png", f"Swift: {sc}") for sc in spec["scenes"] if (REFERENCE / f"{sc}.png").exists()]
        out.append("</div>")
    (COMPARE / "index.html").write_text("\n".join(out))
    print(f"page: {COMPARE / 'index.html'}")
    return True


if __name__ == "__main__":
    verb, arg = (sys.argv + [None, None])[1:3]
    actions = {"check": check, "swift": lambda: swift(arg), "avalonia": lambda: avalonia(arg), "page": page}
    if verb not in actions:
        sys.exit(__doc__)
    sys.exit(0 if actions[verb]() else 1)
