#!/usr/bin/env bash
# Linux: Velopack's Orion AppImage, with the update feed beside it, in
# OUT_DIR/releases from a built tree (decisions #376, #389).
# The shell self-contained, the shim and every library it loads that is not part
# of the base system, the shaders, lensfun, and the licence notices.
#
#   tools/package-linux.sh SHELL_DIR BUILD_DIR OUT_DIR
#
# Needs: dotnet, mksquashfs (squashfs-tools), and vpk on PATH (or $VPK). Run on the oldest
# system the AppImage should start on: it carries no glibc of its own.
set -euo pipefail
shell=$1 build=$(cd "$2" && pwd) out=$3   # build absolute: ldd answers absolute paths
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(git -C "$root" describe --tags --always)
app=$out/Orion.AppDir
rm -rf "$app"; mkdir -p "$app/usr/bin" "$app/usr/lib" "$app/usr/share/doc/orion/licenses"

dotnet publish "$shell" -c Release -r linux-x64 --self-contained -o "$app/usr/bin"
cp "$build/shell-native/liborion_shim.so" "$app/usr/bin/"
cp -R "$build/shaders" "$app/usr/bin/shaders"
mkdir -p "$app/usr/bin/data"; cp -R "$root/data/lensfun" "$app/usr/bin/data/lensfun"

# Every library the shim needs, except the base system's and the GPU's: the C
# and C++ runtimes and the Vulkan loader come from the machine it runs on. The
# rest is vcpkg's (decision #376), never the distribution's: Ubuntu's OpenCV
# brought GDAL and with it poppler, which is GPL.
base='^(linux-vdso|ld-linux|lib(c|m|dl|pthread|rt|stdc\+\+|gcc_s|vulkan|GL|GLX|GLdispatch|EGL|X[A-Za-z0-9]*|xcb[a-z0-9-]*)\.so)'
ldd "$app/usr/bin/liborion_shim.so" | awk '$3 ~ /^\// {print $1, $3}' | while read -r name path; do
    if [[ $name =~ $base ]]; then continue; fi
    case $path in
        "$build"/vcpkg_installed/*) cp -L "$path" "$app/usr/lib/" ;;
        *) echo "error: $name comes from $path, not from vcpkg; the package takes no distribution library" >&2; exit 1 ;;
    esac
done
cp "$root/LICENSE" "$root/NOTICE" "$app/usr/share/doc/orion/"
cp "$root"/licenses/* "$app/usr/share/doc/orion/licenses/"
rm -f "$app"/usr/bin/*.pdb   # debug databases carry build paths; the gate refuses any that remain

cat > "$app/AppRun" <<'EOF'
#!/bin/sh
here="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$here/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$here/usr/bin/Orion" "$@"
EOF
chmod +x "$app/AppRun"
cat > "$app/orion.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Orion
Comment=Fast, subscription-free RAW photo editor
Exec=Orion %F
Icon=orion
Categories=Graphics;Photography;
EOF
cp "$root/brand/orion_logo_system_luminous.svg" "$app/orion.svg"

# Every licence text collected and every file mapped to one, no GPL, nobody named.
python3 "$root/tools/package-gates.py" linux "$app" "$app/usr/share/doc/orion/licenses" "$build" "$shell"

# Velopack (#389), in place of appimagetool: the AppImage, which updates itself,
# and the feed beside it in OUT_DIR/releases. vpk takes this AppDir as it is (its
# name must end in .AppDir) and adds UpdateNix and sq.version to usr/bin. The
# version is the tag without its v, SemVer as vpk needs.
"${VPK:-vpk}" pack --packId Orion --packVersion "${version#v}" --packDir "$app" --mainExe Orion \
    --packTitle Orion --outputDir "$out/releases"
ls "$out/releases"
