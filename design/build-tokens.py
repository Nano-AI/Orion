#!/usr/bin/env python3
"""Generate design-token output from tokens.json.

Writes:
  design/mockup-darkroom.html   CSS custom properties, between TOKENS markers
  ../shell/Shared/Palette.g.cs   C# constants for the app (#315)

Run after any edit to tokens.json.  Never hand-edit the generated regions.
"""

import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).parent
TOKENS = HERE / "tokens.json"
MOCKUP = HERE / "mockup-darkroom.html"
CSHARP = HERE.parent / "shell" / "Shared" / "Palette.g.cs"

START = "/* TOKENS:START — generated from tokens.json, do not edit by hand */"
END = "/* TOKENS:END */"


def load():
    with TOKENS.open() as f:
        return json.load(f)


def strip_comments(d):
    return {k: v for k, v in d.items() if not k.startswith("$")}


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


def main():
    t = load()

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


if __name__ == "__main__":
    main()
