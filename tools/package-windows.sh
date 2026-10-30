#!/usr/bin/env bash
# Windows: Velopack's Orion-win-Setup.exe and Orion-win-Portable.zip, with the
# update feed beside them, in OUT_DIR/releases from a built tree (decisions #376,
# #389); web/js/download.js offers the Setup.exe. The shell self-contained, the
# shim with the vcpkg DLLs vcpkg copied beside it, the shaders, lensfun, and the
# licence notices (vcpkg's copyright file per port).
#
#   tools/package-windows.sh SHELL_DIR BUILD_DIR OUT_DIR
#
# Runs in Git Bash. Needs dotnet, and vpk on PATH (or $VPK).
set -euo pipefail
shell=$1 build=$2 out=$3
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(git -C "$root" describe --tags --always)
app=$out/Orion
rm -rf "$app"; mkdir -p "$app/licenses"

dotnet publish "$shell" -c Release -r win-x64 --self-contained -o "$app"
cp "$build"/shell-native/*.dll "$app/"
cp -R "$build/shaders" "$app/shaders"
mkdir -p "$app/data"; cp -R "$root/data/lensfun" "$app/data/lensfun"

rm -f "$app"/*.pdb   # debug databases carry build paths; the gate refuses any that remain

cp "$root/LICENSE" "$root/NOTICE" "$app/"
cp "$root"/licenses/* "$app/licenses/"
# Every licence text collected and every file mapped to one, no GPL, nobody named.
python "$root/tools/package-gates.py" windows "$app" "$app/licenses" "$build" "$shell"

# Velopack (#389), in place of Inno Setup and the zip: Setup.exe installs per
# user, with no administrator prompt, into %LocalAppData%\Orion, with Start menu
# and desktop shortcuts and an uninstaller in Settings; Portable.zip is the zip.
# The version is the tag without its v, SemVer as vpk needs.
"${VPK:-vpk}" pack --packId Orion --packVersion "${version#v}" --packDir "$app" --mainExe Orion.exe \
    --packTitle Orion --outputDir "$out/releases"
ls "$out/releases"
