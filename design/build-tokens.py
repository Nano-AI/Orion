#!/usr/bin/env python3
"""Generate design-token output from tokens.json.

Writes:
  design/mockup-darkroom.html   CSS custom properties, between TOKENS markers
  ../shell/Shared/Palette.g.cs   C# constants for the app (#315)
  ../web/css/tokens.css          the website's palette roles (#383)

Run after any edit to tokens.json.  Never hand-edit the generated regions.

The palette `activePalette` names fills the colour roles both outputs read. A
role names one of the palette's slots, or a neutral by name (`onAccent` is
`ground`), which the app takes from tokens.json and the site from its own greys
in web/css/base.css. Before anything is written, EVERY palette is checked, and
a failure exits 1 having written nothing:

  - each role pair in check() meets its WCAG 2.x contrast bar, against the
    app's greys and against the site's
  - no palette puts a red or pink within 25 degrees of OKLCH hue of TikTok's
    red beside a cyan (H-alpha, 656.3 nm, renders #FF4356: that red)
"""

import json
import math
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).parent
TOKENS = HERE / "tokens.json"
MOCKUP = HERE / "mockup-darkroom.html"
CSHARP = HERE.parent / "shell" / "Shared" / "Palette.g.cs"
WEB_CSS = HERE.parent / "web" / "css" / "tokens.css"
WEB_BASE = HERE.parent / "web" / "css" / "base.css"
PAPER = "#E4E4E1"        # the research pages' paper at its darkest stop (web/css/sections.css .paper)
TIKTOK_RED_HUE = 18.7    # OKLCH hue of TikTok's #FE2C55

START = "/* TOKENS:START — generated from tokens.json, do not edit by hand */"
END = "/* TOKENS:END */"


def strip_comments(d):
    return {k: v for k, v in d.items() if not k.startswith("$")}


def app_greys(t):
    return {k: v["hex"] for k, v in strip_comments(t["color"]).items() if "hex" in v}


def web_greys():
    return dict(re.findall(r"--([a-z0-9-]+):\s*(#[0-9a-fA-F]{6})", WEB_BASE.read_text()))


def roles(t, name, greys):
    """A palette's roles as hex: a slot's value, or a neutral's from `greys`."""
    p = t["palettes"][name]
    one = lambda ref: p["slots"].get(ref) or greys[ref]
    return {role: [one(r) for r in ref] if isinstance(ref, list) else one(ref) for role, ref in p["roles"].items()}


def load():
    with TOKENS.open() as f:
        t = json.load(f)
    greys = app_greys(t)
    active = roles(t, t["activePalette"], greys)
    for spec in strip_comments(t["color"]).values():
        if "role" in spec:
            spec["hex"] = active[spec["role"]]
    return t


def linear(hex_):
    c = (int(hex_[i:i + 2], 16) / 255 for i in (1, 3, 5))
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]


def contrast(a, b):
    """WCAG 2.x contrast ratio."""
    y = lambda h: sum(w * c for w, c in zip((0.2126, 0.7152, 0.0722), linear(h)))
    hi, lo = sorted((y(a), y(b)), reverse=True)
    return (hi + 0.05) / (lo + 0.05)


def oklch(hex_):
    """Lightness, chroma, hue in degrees: Björn Ottosson's OKLab (2020)."""
    r, g, b = linear(hex_)
    l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
    m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
    s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
    a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s
    bb = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s
    return 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, math.hypot(a, bb), math.degrees(math.atan2(bb, a)) % 360


def check(t):
    """Every palette against every bar. The failures, one line each."""
    bad = []
    for name in strip_comments(t["palettes"]):
        for where, greys in (("app", app_greys(t)), ("web", web_greys())):
            r = roles(t, name, greys)
            pairs = [(f"accent as text on {k}", r["accent"], greys[k], 4.5) for k in ("ground", "panel", "raised")]
            pairs += [
                ("accent ring on ground", r["accent"], greys["ground"], 3),
                ("onAccent on accent", r["onAccent"], r["accent"], 4.5),
                # status carries words (the footer's failure line), so the text bar, not the glyph's 3
                ("status on panel", r["status"], greys["panel"], 4.5),
                ("mark on ground", r["mark"], greys["ground"], 3),
            ]
            if where == "web":
                pairs.append(("accentInk on the research paper", r["accentInk"], PAPER, 4.5))
            bad += [f"{name} ({where}): {what} is {contrast(fg, bg):.2f}:1, needs {bar}"
                    for what, fg, bg, bar in pairs if contrast(fg, bg) < bar]
        hues = [oklch(h) for h in strip_comments(t["palettes"][name]["slots"]).values()]
        cyan = any(c > 0.05 and 170 <= h <= 230 for _, c, h in hues)
        red = [h for _, c, h in hues if c > 0.05 and abs((h - TIKTOK_RED_HUE + 180) % 360 - 180) < 25]
        if cyan and red:
            bad.append(f"{name}: a red or pink at hue {red[0]:.0f} beside a cyan reads as TikTok's palette")
    return bad


def css_block(t):
    lines = [START, "  :root {"]

    lines.append("    /* Color */")
    for name, spec in strip_comments(t["color"]).items():
        lines.append(f"    --{name}: {spec['hex']};  /* {spec['use']} */")

    lines.append("")
    lines.append("    /* Space */")
    for name, px in strip_comments(t["space"]).items():
        lines.append(f"    --space-{name}: {px}px;")

    lines.append("")
    lines.append("    /* Radius */")
    for name, px in strip_comments(t["radius"]).items():
        lines.append(f"    --radius-{name}: {px}px;")

    lines.append("")
    lines.append("    /* Type */")
    for name, v in strip_comments(t["type"]).items():
        val = f"{v}px" if isinstance(v, (int, float)) else v
        lines.append(f"    --type-{name}: {val};")

    lines.append("")
    lines.append("    /* Layout */")
    for name, px in strip_comments(t["layout"]).items():
        lines.append(f"    --layout-{name}: {px}px;")

    lines.append("")
    lines.append("")
    lines.append("    /* Shadow: the one lift */")
    sh = strip_comments(t["shadow"])
    lines.append(f"    --shadow-lift: 0 {sh['y']}px {sh['blur']}px rgba(0, 0, 0, {sh['alpha']});")

    lines.append("")
    lines.append("    /* Icon */")
    for name, v in strip_comments(t["icon"]).items():
        lines.append(f"    --icon-{name}: {v}px;")

    lines.append("")
    lines.append("    /* Motion */")
    for name, v in strip_comments(t["motion"]).items():
        lines.append(f"    --motion-{name}: {v};")

    lines.append("  }")
    lines.append("  " + END)
    return "\n".join(lines)


def csharp_file(t):
    """The tokens as C# constants for the app (#315)."""
    out = [
        "// GENERATED from design/tokens.json by design/build-tokens.py.",
        "// Do not edit by hand — your changes will be overwritten.",
        f"// Palette: {t['activePalette']}.",
        "",
        "using Avalonia.Media;",
        "using Avalonia.Media.Immutable;",
        "",
        "namespace OrionShell;",
        "",
        "public static class Palette {",
    ]
    for name, spec in strip_comments(t["color"]).items():
        r, g, b = (int(spec["hex"][i:i + 2], 16) for i in (1, 3, 5))
        out.append(f"    /// {spec['use']}")
        out.append(f"    public static readonly IBrush {name[0].upper() + name[1:]} = "
                   f"new ImmutableSolidColorBrush(Color.FromRgb({r}, {g}, {b}));")
    out.append("}")
    for group, cls in (("space", "Space"), ("radius", "Radius"),
                       ("type", "TypeSize"), ("layout", "Layout"),
                       ("shadow", "Shadow"), ("icon", "IconSize")):
        out += ["", f"public static class {cls} {{"]
        for name, v in strip_comments(t[group]).items():
            if isinstance(v, (int, float)):
                out.append(f"    public const double {name[0].upper() + name[1:]} = {v};")
            else:   # a font family, say: Shared/Design/Type.cs loads `numeral` by this name
                out.append(f'    public const string {name[0].upper() + name[1:]} = "{v}";')
        out.append("}")
    return "\n".join(out) + "\n"


def web_css(t):
    """The site's palette roles. Its neutrals stay hand-written in base.css, so a
    role that names a neutral is written as that neutral's variable."""
    name = t["activePalette"]
    p = t["palettes"][name]
    kebab = lambda s: re.sub(r"[A-Z]", lambda m: "-" + m.group(0).lower(), s)
    value = lambda ref: p["slots"][ref].lower() if ref in p["slots"] else f"var(--{kebab(ref)})"
    out = ["/* GENERATED from design/tokens.json by design/build-tokens.py. Do not edit by hand.",
           f" * Palette: {name}. To swap, set activePalette in tokens.json and run build-tokens.py. */",
           "", ":root {"]
    for role, ref in p["roles"].items():
        v = f"linear-gradient(90deg, {', '.join(map(value, ref))})" if isinstance(ref, list) else value(ref)
        out.append(f"  --{kebab(role)}: {v};")
    return "\n".join(out + ["}"]) + "\n"


def main():
    t = load()
    bad = check(t)
    if bad:
        sys.exit("build-tokens: a palette fails its bars; nothing written.\n  " + "\n  ".join(bad))

    if MOCKUP.exists():
        html = MOCKUP.read_text()
        pattern = re.compile(
            re.escape(START) + ".*?" + re.escape(END), re.DOTALL
        )
        if not pattern.search(html):
            sys.exit(f"error: TOKENS markers not found in {MOCKUP.name}")
        MOCKUP.write_text(pattern.sub(lambda _: css_block(t), html))
        print(f"wrote CSS tokens -> {MOCKUP.relative_to(HERE.parent)}")

    CSHARP.parent.mkdir(parents=True, exist_ok=True)
    CSHARP.write_text(csharp_file(t))
    print(f"wrote C# tokens -> {CSHARP.relative_to(HERE.parent)}")

    WEB_CSS.write_text(web_css(t))
    print(f"wrote web palette -> {WEB_CSS.relative_to(HERE.parent)} "
          f"({t['activePalette']}; every palette met its contrast bars)")


if __name__ == "__main__":
    main()
