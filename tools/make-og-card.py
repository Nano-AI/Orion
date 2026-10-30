#!/usr/bin/env python3
"""Draw the social preview card, web/img/og-orion.jpg or .png (1200 x 630).

    ./tools/make-og-card.py [--copy OUT.png]

Writes whichever of JPEG (quality 92, 4:4:4) and PNG is smaller, removing the
other; the pages' og:image and twitter:image name .jpg, so a switch to .png
means editing both pages. --copy also saves a PNG of the card at OUT.png.

Book of Shapes' "Node Garden" in the site's greys, three nodes on a rising
diagonal picked out as Orion's Belt, and the card's words in the site's own
faces. Every colour is read from the site's own variables: the greys from
web/css/base.css, the belt from --mark in web/css/tokens.css, which
design/build-tokens.py writes from the active palette. A palette change is
build-tokens.py, then this (decision #383).

⚠ The pattern's licence (bookofshapes.com/license) allows it as part of a
product and forbids handing it on as a pattern. Its numbers live in one place,
the empty-window drawing shell/Slices/Canvas/NodeGarden.cs, and this reads them
from there: no second copy, and no pattern file anywhere in the tree.

Renders with headless Chrome under a throwaway profile (never the person's own
browser, never a window), then writes PNG or JPEG, whichever is smaller, with
no metadata (check-site rule i).
"""
import io
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
W, H = 1200, 630


def garden():
    # Node Garden pattern © Nikolaj Sokolowski (bookofshapes.com), used under its licence as part of
    # Orion's interface. Not licensed for reuse as a pattern; do not copy it out of NodeGarden.cs.
    src = (ROOT / "shell/Slices/Canvas/NodeGarden.cs").read_text()
    nums = lambda name: [int(n) for n in re.findall(r"\d+", re.search(name + r' = """(.*?)"""', src, re.S).group(1))]
    c, l = nums("NodeData"), nums("LinkData")
    return list(zip(c[::2], c[1::2])), [tuple(l[i:i + 4]) for i in range(0, len(l), 4)]


def tokens():
    css = (ROOT / "web/css/tokens.css").read_text() + (ROOT / "web/css/base.css").read_text()
    return {k: re.search(rf"--{k}:\s*(#[0-9a-fA-F]{{6}})", css).group(1) for k in ("ground", "line", "raised", "text", "dim", "mark")}


def card(t):
    nodes, links = garden()
    near = lambda x, y: min(nodes, key=lambda p: (p[0] - x) ** 2 + (p[1] - y) ** 2)
    belt = [near(148, 211), near(189, 190), near(227, 167)]   # a rising diagonal, as the belt rises
    art = [f'<line x1="{a}" y1="{b}" x2="{c}" y2="{d}"/>' for a, b, c, d in links]
    art += [f'<circle cx="{x}" cy="{y}" r="2.8" stroke="none" fill="var(--line)"/>' for x, y in nodes]
    art.append('<polyline points="' + " ".join(f"{x},{y}" for x, y in belt) + '" fill="none" stroke="var(--mark)" stroke-width="1.6"/>')
    art += [f'<circle cx="{x}" cy="{y}" r="5" stroke="none" fill="var(--mark)"/>' for x, y in belt]
    fonts = (ROOT / "web/fonts").as_uri()
    root = "".join(f"--{k}:{v};" for k, v in t.items())
    return f'''<!doctype html><meta charset="utf-8"><style>
@font-face{{font-family:Archivo;src:url("{fonts}/Archivo-var.woff2");font-weight:100 900}}
@font-face{{font-family:"Martian Mono";src:url("{fonts}/MartianMono-var.woff2");font-weight:100 800}}
:root{{{root}}}
html,body{{margin:0;width:{W}px;height:{H}px;overflow:hidden;background:var(--ground);color:var(--text)}}
.art{{position:absolute;right:0;top:0;width:640px;height:{H}px;-webkit-mask-image:linear-gradient(90deg,transparent,black 35%)}}
.art svg{{width:100%;height:100%}} .art line{{stroke:var(--raised);stroke-width:.9;stroke-linecap:round}}
.words{{position:absolute;left:72px;top:150px;font-family:Archivo,sans-serif}}
.mark{{display:flex;gap:10px;transform:rotate(-24deg);transform-origin:left;width:max-content}}
.mark i{{width:14px;height:14px;border-radius:50%;background:var(--mark)}}
h1{{font-size:112px;font-weight:700;letter-spacing:-.03em;margin:12px 0 18px;line-height:1}}
p{{font-size:26px;line-height:1.35;color:color-mix(in srgb,var(--text) 82%,var(--ground));margin:0 0 28px}}
.url{{font-family:"Martian Mono",monospace;font-size:18px;color:var(--dim)}}
</style>
<div class="art"><svg viewBox="0 0 376 396" preserveAspectRatio="xMidYMid slice">{"".join(art)}</svg></div>
<div class="words"><div class="mark"><i></i><i></i><i></i></div><h1>Orion</h1>
<p>A RAW photo editor for macOS.<br>Every slider move at full resolution.<br>No subscription.</p>
<span class="url">bankoti.dev/Orion</span></div>'''


def main():
    with tempfile.TemporaryDirectory() as tmp:
        page, shot = Path(tmp, "card.html"), Path(tmp, "card.png")
        page.write_text(card(tokens()))
        subprocess.run([CHROME, "--headless=new", f"--user-data-dir={tmp}/profile", "--no-first-run", "--hide-scrollbars",
                        "--force-device-scale-factor=1", f"--window-size={W},{H}", "--virtual-time-budget=3000",
                        f"--screenshot={shot}", page.as_uri()], check=True, capture_output=True, timeout=120)
        img = Image.open(shot).convert("RGB")
    assert img.size == (W, H), img.size
    png, jpg = io.BytesIO(), io.BytesIO()
    img.save(png, "PNG", optimize=True)
    img.save(jpg, "JPEG", quality=92, subsampling=0, optimize=True)   # 4:4:4: thin cyan lines smear under 4:2:0
    kind, data = min((("png", png), ("jpg", jpg)), key=lambda kv: kv[1].tell())
    out = ROOT / "web/img" / f"og-orion.{kind}"
    for stale in (ROOT / "web/img").glob("og-orion.*"):
        stale.unlink()
    out.write_bytes(data.getvalue())
    print(f"{out.relative_to(ROOT)}: {data.tell()} bytes (png {png.tell()}, jpg {jpg.tell()})")
    if "--copy" in sys.argv:
        img.save(sys.argv[sys.argv.index("--copy") + 1], "PNG", optimize=True)


if __name__ == "__main__":
    main()
