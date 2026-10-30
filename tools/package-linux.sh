#!/usr/bin/env bash
# Linux: Orion-<version>-linux-x86_64.AppImage from a built tree (decision #376).
# The shell self-contained, the shim and every library it loads that is not part
# of the base system, the shaders, lensfun, and the licence notices.
#
#   tools/package-linux.sh SHELL_DIR BUILD_DIR OUT_DIR
#
# Needs: dotnet, and appimagetool on PATH (or $APPIMAGETOOL). Run on the oldest
# system the AppImage should start on: it carries no glibc of its own.
set -euo pipefail
shell=$1 build=$2 out=$3
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(git -C "$root" describe --tags --always --dirty)
app=$out/AppDir
rm -rf "$app"; mkdir -p "$app/usr/bin" "$app/usr/lib" "$app/usr/share/doc/orion/licenses"

dotnet publish "$shell" -c Release -r linux-x64 --self-contained -o "$app/usr/bin"
cp "$build/shell-native/liborion_shim.so" "$app/usr/bin/"
cp -R "$build/shaders" "$app/usr/bin/shaders"
mkdir -p "$app/usr/bin/data"; cp -R "$root/data/lensfun" "$app/usr/bin/data/lensfun"

# Every library the shim needs, except the base system's and the GPU's: the C
# and C++ runtimes and the Vulkan loader come from the machine it runs on.
base='^(linux-vdso|ld-linux|libc|libm|libdl|libpthread|librt|libstdc\+\+|libgcc_s|libvulkan|libX|libxcb|libGL|libEGL)\b'
ldd "$app/usr/bin/liborion_shim.so" | awk '$3 ~ /^\// {print $1, $3}' | while read -r name path; do
    [[ $name =~ $base ]] && continue
    cp -L "$path" "$app/usr/lib/"
    # The Debian package's copyright file is that library's licence.
    pkg=$(dpkg -S "$path" 2>/dev/null | head -1 | cut -d: -f1) || true
    [[ -n $pkg && -f /usr/share/doc/$pkg/copyright ]] && cp "/usr/share/doc/$pkg/copyright" "$app/usr/share/doc/orion/licenses/$pkg.txt"
done
cp "$root/LICENSE" "$root/NOTICE" "$app/usr/share/doc/orion/"
cp "$root"/licenses/* "$app/usr/share/doc/orion/licenses/"

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

ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "${APPIMAGETOOL:-appimagetool}" "$app" "$out/Orion-$version-linux-x86_64.AppImage"
echo "$out/Orion-$version-linux-x86_64.AppImage"
