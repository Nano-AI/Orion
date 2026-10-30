#!/usr/bin/env bash
# Windows: Orion-<version>-win-x64.zip and Orion-<version>-win-x64-setup.exe from a
# built tree (decision #376), named so web/js/download.js offers them. The shell
# self-contained, the shim with the vcpkg DLLs vcpkg copied beside it, the
# shaders, lensfun, and the licence notices (vcpkg's copyright file per port).
#
#   tools/package-windows.sh SHELL_DIR BUILD_DIR OUT_DIR
#
# Runs in Git Bash. Needs dotnet, 7z (or zip), and Inno Setup's ISCC.exe on PATH
# (or $ISCC) for the installer.
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

cp "$root/LICENSE" "$root/NOTICE" "$app/"
cp "$root"/licenses/* "$app/licenses/"
for c in "$build"/vcpkg_installed/*/share/*/copyright; do
    port=$(basename "$(dirname "$c")")
    cp "$c" "$app/licenses/$port.txt"
done

zip=$out/Orion-$version-win-x64.zip
rm -f "$zip"
(cd "$out" && 7z a -tzip -bd "$(basename "$zip")" Orion >/dev/null)

# Inno Setup: a per-user install, no administrator prompt, an uninstaller.
cat > "$out/orion.iss" <<EOF
[Setup]
AppName=Orion
AppVersion=$version
AppPublisher=Orion
DefaultDirName={localappdata}\\Programs\\Orion
DefaultGroupName=Orion
PrivilegesRequired=lowest
OutputDir=.
OutputBaseFilename=Orion-$version-win-x64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=Orion\\LICENSE

[Files]
Source: "Orion\\*"; DestDir: "{app}"; Flags: recursesubdirs

[Icons]
Name: "{group}\\Orion"; Filename: "{app}\\Orion.exe"
EOF
(cd "$out" && "${ISCC:-iscc}" //Q orion.iss)
ls "$out"/Orion-"$version"-win-x64*
