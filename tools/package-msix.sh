#!/usr/bin/env bash
# Windows: the Microsoft Store's Orion-<version>-x64.msix in OUT_DIR/store, from the
# files tools/package-windows.sh staged in OUT_DIR/Orion. Unsigned: the Store signs
# what it publishes. The identity is Partner Center's, from MSSTORE_IDENTITY_NAME,
# MSSTORE_PUBLISHER and MSSTORE_PUBLISHER_DISPLAY_NAME; without them the package
# carries placeholders and installs only for a test. The app turns its own updates
# off inside it (Updates.Packaged).
#
#   tools/package-msix.sh SHELL_DIR BUILD_DIR OUT_DIR
#
# Runs in Git Bash, after package-windows.sh. Needs makeappx on PATH (or $MAKEAPPX):
# the Windows SDK's, which the MSVC developer environment puts there.
set -euo pipefail
shell=$1 build=$2 out=$3
root=$(cd "$(dirname "$0")/.." && pwd)
layout=$out/msix
rm -rf "$layout" "$out/store"; mkdir -p "$out/store"
cp -R "$out/Orion" "$layout"
mkdir -p "$layout/Assets"; cp "$root"/brand/windows/store/*.png "$layout/Assets/"

# Four numbers, the first not 0 and the last 0, which the Store keeps for itself:
# MAJOR*1000+MINOR . PATCH . commits . 0, rising with every tag and every commit.
# v0.6.0-alpha.2-4-g258640d at 999 commits is 6.0.999.0.
describe=$(git -C "$root" describe --tags --always)
[[ $describe =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+) ]] || { echo "error: $describe does not start vMAJOR.MINOR.PATCH" >&2; exit 1; }
version=$((BASH_REMATCH[1] * 1000 + BASH_REMATCH[2])).${BASH_REMATCH[3]}.$(git -C "$root" rev-list --count HEAD).0

# The placeholders are valid but no one's: a Publisher must be a distinguished name.
python - "$root/tools/AppxManifest.xml" "$shell/Program.cs" "$version" > "$layout/AppxManifest.xml" <<'PY'
import os, re, sys
from xml.sax.saxutils import escape
template, program, version = sys.argv[1:4]
raws = re.search(r"RawExtensions = \[(.*?)\];", open(program).read(), re.S).group(1)
fields = {
    "NAME": os.environ.get("MSSTORE_IDENTITY_NAME") or "Orion.Placeholder",
    "PUBLISHER": os.environ.get("MSSTORE_PUBLISHER") or "CN=00000000-0000-0000-0000-000000000000",
    "PUBLISHER_DISPLAY_NAME": os.environ.get("MSSTORE_PUBLISHER_DISPLAY_NAME") or "Orion (placeholder)",
    "VERSION": version,
}
text = open(template).read()
for k, v in fields.items():
    text = text.replace(f"@{k}@", escape(v, {'"': "&quot;"}))
text = text.replace("@FILE_TYPES@", "".join(f"<uap:FileType>.{e}</uap:FileType>" for e in re.findall(r'"(\w+)"', raws)))
sys.stdout.write(text)
PY
# The same gates as the staged files, over what the package adds: the manifest, the tiles.
python "$root/tools/package-gates.py" windows "$layout" "$layout/licenses" "$build" "$shell"

msix=$out/store/Orion-$version-x64.msix
# makeappx's /flags are not paths: Git Bash must not rewrite them.
MSYS2_ARG_CONV_EXCL='*' "${MAKEAPPX:-makeappx}" pack /o /d "$(cygpath -w "$layout")" /p "$(cygpath -w "$msix")"
ls -la "$out/store"
