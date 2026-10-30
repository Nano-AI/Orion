#!/usr/bin/env python3
"""The license notice for what esbuild bundled into orion-mcp.

    tools/mcp-licenses.py <esbuild metafile> <mcp dir> <output file>

Every npm package with a file in the bundle, read from the metafile rather than
from package.json, because the bundle is what ships and a dependency nobody
listed is still in it. Each package's own LICENSE text is copied verbatim; the
MIT and ISC terms require the notice to travel with the binary.

⚠ **Fails on any license outside a permissive allow-list, or a package with no
license file.** Orion is source-available, not open source (#359), so a copyleft package arriving as a
transitive dependency of the MCP SDK would be a licensing change nobody chose;
this is where it would be noticed. The SDK's tree was all MIT when this was
written (#310).
"""

import json
import re
import sys
from pathlib import Path

PERMISSIVE = {"MIT", "ISC", "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0", "0BSD"}
LICENSE_FILES = ("LICENSE", "LICENSE.md", "LICENSE.txt", "LICENCE", "license", "license.md")


def main() -> int:
    meta, mcp, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    dirs = set()
    for source in json.loads(meta.read_text())["inputs"]:
        found = re.match(r"(.*node_modules/(?:@[^/]+/)?[^/]+)/", source)
        if found:
            dirs.add(found.group(1))

    problems, sections = [], []
    for rel in sorted(dirs):
        pkg = mcp / rel
        info = json.loads((pkg / "package.json").read_text())
        name, license = info["name"], info.get("license", "")
        text = next((pkg / f for f in LICENSE_FILES if (pkg / f).is_file()), None)
        if license not in PERMISSIVE:
            problems.append(f"{name}: license {license or 'missing'} is not on the allow-list")
        if text is None:
            problems.append(f"{name}: no license file to carry")
            continue
        sections.append(f"{name} {info['version']} ({license})\n\n{text.read_text().strip()}\n")

    if problems:
        print("error: orion-mcp's bundle:\n  " + "\n  ".join(problems), file=sys.stderr)
        return 1
    out.write_text("Packages bundled into Contents/MacOS/orion-mcp\n\n"
                   + ("\n" + "-" * 72 + "\n\n").join(sections))
    print(f"  licenses  {len(sections)} npm packages in orion-mcp, all permissive")
    return 0


if __name__ == "__main__":
    sys.exit(main())
