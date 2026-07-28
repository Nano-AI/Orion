#!/usr/bin/env python3
"""Generate design-token output from tokens.json.

Writes:
  design/mockup-darkroom.html   CSS custom properties, between TOKENS markers
  ../Sources/OrionUI/DesignTokens.swift   Swift constants for the app

Run after any edit to tokens.json.  Never hand-edit the generated regions.
"""

import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).parent
TOKENS = HERE / "tokens.json"
MOCKUP = HERE / "mockup-darkroom.html"
SWIFT = HERE.parent / "Sources" / "OrionUI" / "DesignTokens.swift"

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
    lines.append("    /* Motion */")
    for name, v in strip_comments(t["motion"]).items():
        lines.append(f"    --motion-{name}: {v};")

    lines.append("  }")
    lines.append("  " + END)
    return "\n".join(lines)


def camel(name):
    return name[0].lower() + name[1:]


def swift_file(t):
    out = [
        "// GENERATED from design/tokens.json by design/build-tokens.py.",
        "// Do not edit by hand — your changes will be overwritten.",
        "",
        "import SwiftUI",
        "",
        "public enum Orion {",
        "",
        "    // MARK: Palette",
        "    //",
        "    // Neutrals are deliberately near-perfectly neutral: a tinted interface",
        "    // reads as a color cast and corrupts the judgment the app exists to support.",
        "    public enum Palette {",
    ]
    for name, spec in strip_comments(t["color"]).items():
        hexv = spec["hex"].lstrip("#")
        out.append(f"        /// {spec['use']}")
        out.append(f"        public static let {camel(name)} = Color(hex: 0x{hexv.upper()})")
    out.append("    }")

    out += ["", "    // MARK: Space", "    public enum Space {"]
    for name, px in strip_comments(t["space"]).items():
        out.append(f"        public static let {camel(name)}: CGFloat = {px}")
    out.append("    }")

    out += ["", "    // MARK: Radius", "    public enum Radius {"]
    for name, px in strip_comments(t["radius"]).items():
        out.append(f"        public static let {camel(name)}: CGFloat = {px}")
    out.append("    }")

    out += ["", "    // MARK: Type sizes", "    public enum TypeSize {"]
    for name, v in strip_comments(t["type"]).items():
        if isinstance(v, (int, float)):
            out.append(f"        public static let {camel(name)}: CGFloat = {v}")
    out.append("    }")

    out += ["", "    // MARK: Layout", "    public enum Layout {"]
    for name, px in strip_comments(t["layout"]).items():
        out.append(f"        public static let {camel(name)}: CGFloat = {px}")
    out.append("    }")

    out += [
        "}",
        "",
        "extension Color {",
        "    /// 0xRRGGBB literal, in the display-P3 space the app renders in.",
        "    init(hex: UInt32) {",
        "        self.init(",
        "            .displayP3,",
        "            red:   Double((hex >> 16) & 0xFF) / 255,",
        "            green: Double((hex >>  8) & 0xFF) / 255,",
        "            blue:  Double( hex        & 0xFF) / 255",
        "        )",
        "    }",
        "}",
        "",
    ]
    return "\n".join(out)


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

    SWIFT.parent.mkdir(parents=True, exist_ok=True)
    SWIFT.write_text(swift_file(t))
    print(f"wrote Swift tokens -> {SWIFT.relative_to(HERE.parent)}")


if __name__ == "__main__":
    main()
