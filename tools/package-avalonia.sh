#!/bin/bash
#
# Packages the Avalonia shell (spikes/avalonia-shell) as a Mac app a stranger
# can run: "Orion Avalonia.app" and a disk image, ad-hoc signed, not notarized.
#
# tools/package-app.sh solved every one of these problems for the Swift app
# first, and its ⚠ notes are why the steps below look the way they do. Read it
# before changing this. What is different here:
#
#   1. It builds from a clean tree: the engine (CMake), the shim
#      (native/build.sh), then a self-contained `dotnet publish` whose whole
#      output is Contents/MacOS. The .NET apphost finds its runtime beside
#      itself, and the engine (ResourcePaths.cpp) finds Contents/Resources
#      beside the apphost, so no working directory is needed.
#   2. Homebrew dylibs hang off more than one root: the shim (libraw, OpenCV),
#      which .NET dlopens, and Homebrew's source-built .NET runtime itself,
#      whose compression library links Homebrew's brotli where Microsoft's
#      build links it statically. So the walk starts from every Mach-O in
#      Contents/MacOS, and each one that reached Homebrew gets the rpath to
#      Contents/Frameworks.
#   3. Its own name and bundle id, "Orion Avalonia" / com.nanoai.orion.avalonia.
#      The Swift app is Orion.app / com.nanoai.orion: the two install side by
#      side in /Applications, and macOS keeps their defaults, saved window state
#      and permissions apart because those are keyed by bundle id.
#
# Usage:  tools/package-avalonia.sh [output-dir] [label]
#         label defaults to `git describe --tags`; the image is
#         Orion-<label>-avalonia.dmg.
# Needs:  cmake + Xcode's Metal toolchain, Homebrew libraw and opencv, and
#         dotnet 10 (DOTNET_ROOT defaults to Homebrew's).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
SRC="$ROOT/spikes/avalonia-shell"
OUT="${1:-$ROOT/dist-avalonia}"
LABEL="${2:-$(git -C "$ROOT" describe --tags)}"
NAME="Orion Avalonia"
BUNDLE_ID="com.nanoai.orion.avalonia"
export DOTNET_ROOT="${DOTNET_ROOT:-/opt/homebrew/opt/dotnet/libexec}"

APP="$OUT/$NAME.app"
MACOS="$APP/Contents/MacOS"
RES="$APP/Contents/Resources"
FW="$APP/Contents/Frameworks"
SHIM="$MACOS/liborion_shim.dylib"

echo "$NAME $LABEL"
rm -rf "$OUT"
mkdir -p "$OUT"

# ── Build ───────────────────────────────────────────────────────────────────
cmake -S "$ROOT" -B "$BUILD" >/dev/null   # needs third_party/slang (see engine/shaders)
cmake --build "$BUILD" --target orion_engine -j4
"$SRC/native/build.sh"

# No PDBs: a PDB, and the debug directory entry that points at it, is an
# absolute path into the checkout, which is the builder's home directory.
dotnet publish "$SRC/OrionShell.csproj" -c Release -r osx-arm64 --self-contained \
    -p:DebugType=None -p:DebugSymbols=false --nologo -v quiet -o "$MACOS"
echo "  publish   $(ls "$MACOS" | wc -l | tr -d ' ') files, .NET $(dotnet --version)"

# ── Resources ───────────────────────────────────────────────────────────────
mkdir -p "$RES/data" "$FW"
cp -R "$BUILD/shaders" "$RES/shaders"
cp -R "$ROOT/data/lensfun" "$RES/data/lensfun"
echo "  shaders   $(ls "$RES/shaders" | wc -l | tr -d ' ') metallibs, data lensfun"

VERSION="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD/CMakeCache.txt")"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>$NAME</string>
    <key>CFBundleDisplayName</key><string>$NAME</string>
    <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
    <key>CFBundleExecutable</key><string>OrionShell</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$VERSION</string>
    <key>CFBundleVersion</key><string>$VERSION</string>
    <key>LSMinimumSystemVersion</key><string>14.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSPrincipalClass</key><string>NSApplication</string>
</dict>
</plist>
PLIST

# ── Licenses ────────────────────────────────────────────────────────────────
#
# Copied verbatim from the installed packages, as tools/package-app.sh does.
mkdir -p "$RES/licenses"
copy_license() {
    if [[ -f "$2" ]]; then cp "$2" "$RES/licenses/$1"
    else echo "warning: license for $1 not found at $2" >&2; fi
}
LIBRAW_DIR="$(dirname "$(dirname "$(readlink -f /opt/homebrew/opt/libraw/lib/libraw_r.dylib)")")"
copy_license "LibRaw-LGPL-2.1.txt" "$LIBRAW_DIR/LICENSE.LGPL"
copy_license "LibRaw-CDDL-1.0.txt" "$LIBRAW_DIR/LICENSE.CDDL"
copy_license "LibRaw-COPYRIGHT.txt" "$LIBRAW_DIR/COPYRIGHT"
for spec in \
    "libomp-Apache-2.0-LLVM.txt:libomp:LICENSE.TXT" \
    "libjpeg-turbo.md:jpeg-turbo:LICENSE.md" \
    "little-cms2-MIT.txt:little-cms2:LICENSE" \
    "opencv-Apache-2.0.txt:opencv:LICENSE" \
    "libjxl-BSD-3-Clause.txt:jpeg-xl:LICENSE" \
    "highway-Apache-2.0-or-BSD-3-Clause.txt:highway:LICENSE" \
    "brotli-MIT.txt:brotli:LICENSE"
do
    IFS=: read -r out pkg file <<< "$spec"
    src="$(ls -d /opt/homebrew/Cellar/"$pkg"/*/"$file" 2>/dev/null | tail -1 || true)"
    copy_license "$out" "${src:-/nonexistent}"
done
# The assistant terminal's NuGet packages (NOTICE carries the MIT text for all
# of them); the two that ship a licence file get it copied verbatim too.
NUGET="${NUGET_PACKAGES:-$HOME/.nuget/packages}"
copy_license "Unicode.net-MIT.txt" "$NUGET/unicode.net/2.0.0/LICENSE"
copy_license "SkiaSharp.HarfBuzz-MIT.txt" "$NUGET/skiasharp.harfbuzz/3.119.4/LICENSE.txt"
cp "$ROOT/NOTICE" "$RES/licenses/NOTICE.txt"
cp "$ROOT/licenses/Apache-2.0.txt" "$RES/licenses/Apache-2.0.txt"   # NOTICE refers to it (Abseil, OpenCV)
echo "  licenses  $(ls "$RES/licenses" | wc -l | tr -d ' ') files"

# ── Dylibs ──────────────────────────────────────────────────────────────────
#
# The dependency walk of tools/package-app.sh, started from every Mach-O in
# Contents/MacOS instead of the one executable. Its ⚠ notes apply line for line: @rpath dependencies count,
# resolve against where a library was copied *from*, read each rpath list once,
# mark seen at enqueue. Change both together.
rpaths_of() {
    otool -l "$1" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}' | grep -v '^@' || true
}

# Load commands only: `otool -L` also prints a dylib's own id, and
# libAvaloniaNative's is /usr/local/lib/..., which loads nothing.
loads_of() {
    otool -l "$1" | awk '/cmd LC_(LOAD|LOAD_WEAK|REEXPORT|LAZY_LOAD|LOAD_UPWARD)_DYLIB/{f=1} f&&/ name /{print $2; f=0}'
}

collect() {
    local target="$1" origin="${2:-}" dep name rp hit rps
    rps="$(rpaths_of "$target")"
    for dep in $(loads_of "$target"); do
        case "$dep" in
            /opt/homebrew/*|/usr/local/*) echo "$dep"; continue ;;
            @rpath/*|@loader_path/*|@executable_path/*) name="${dep##*/}" ;;
            *) continue ;;
        esac
        hit=""
        for rp in $rps; do
            if [[ -f "$rp/$name" ]]; then
                case "$rp" in /opt/homebrew/*|/usr/local/*) hit="$rp/$name"; break ;; esac
            fi
        done
        if [[ -z "$hit" && -n "$origin" && -f "$origin/$name" ]]; then
            case "$origin" in /opt/homebrew/*|/usr/local/*) hit="$origin/$name" ;; esac
        fi
        if [[ -n "$hit" ]]; then echo "$hit"; fi
    done
    return 0
}

SEEN=" "
PENDING=""
enqueue() {
    local path base
    for path in "$@"; do
        base="${path##*/}"
        case "$SEEN" in *" $base "*) continue ;; esac
        SEEN="$SEEN$base "
        PENDING="$PENDING $path"
    done
}

ROOTS=()
while IFS= read -r f; do
    if file -b "$f" | grep -q Mach-O; then ROOTS+=("$f"); fi
done < <(find "$MACOS" -type f)
for f in "${ROOTS[@]}"; do enqueue $(collect "$f"); done
while [[ -n "${PENDING// /}" ]]; do
    set -- $PENDING
    dep="$1"; shift; PENDING="$*"
    base="${dep##*/}"
    [[ -f "$dep" ]] || { echo "error: $dep not found" >&2; exit 1; }
    cp -f "$dep" "$FW/$base"
    chmod u+w "$FW/$base"
    enqueue $(collect "$FW/$base" "$(dirname "$dep")")
done
echo "  dylibs    $(ls "$FW" | wc -l | tr -d ' ') from Homebrew"

# install_name_tool, without the warning it prints on every signed file it
# touches: they are all re-signed below, and hundreds of those buried real errors.
name_tool() {
    install_name_tool "$@" 2> >(grep -v 'will invalidate the code signature' >&2)
}

retarget() {
    local target="$1" dep rp
    for dep in $(loads_of "$target"); do
        case "$dep" in
            /opt/homebrew/*|/usr/local/*)
                name_tool -change "$dep" "@rpath/${dep##*/}" "$target" ;;
        esac
    done
    for rp in $(rpaths_of "$target"); do
        case "$rp" in
            /opt/homebrew/*|/usr/local/*)
                name_tool -delete_rpath "$rp" "$target" 2>/dev/null || true ;;
        esac
    done
}

for f in "${ROOTS[@]}"; do
    if [[ -n "$(collect "$f")" ]]; then
        name_tool -add_rpath "@loader_path/../Frameworks" "$f"
        retarget "$f"
        echo "  retarget  ${f##*/}"
    fi
done
# The shim's id is the path it was linked at, inside the checkout.
name_tool -id "@rpath/liborion_shim.dylib" "$SHIM"
for lib in "$FW"/*.dylib; do
    name_tool -id "@rpath/${lib##*/}" "$lib"
    retarget "$lib"
done

# The engine is RelWithDebInfo: the shim's debug map names every object file in
# the checkout. Keep the symbol table, drop the map, before signing.
strip -S "$SHIM"

# ── Sign, after rewriting ───────────────────────────────────────────────────
#
# Inside out, then the bundle, which signs the apphost with it.
#
# ⚠ **Every file in Contents/MacOS, not only the Mach-Os.** codesign counts
# anything there as nested code, so the managed .dlls and .jsons the publish
# put beside the apphost each need a signature of their own, or signing the
# bundle fails with "code object is not signed at all" naming one of them.
# For a non-Mach-O file that signature lives in extended attributes, which
# cp -R, hdiutil and Finder all carry. Avalonia's macOS deployment guide signs
# the same way.
MACHOS=()
while IFS= read -r f; do
    if file -b "$f" | grep -q Mach-O; then MACHOS+=("$f"); fi
done < <(find "$MACOS" "$FW" -type f)
while IFS= read -r f; do
    err="$(codesign --force --sign - "$f" 2>&1)" || { echo "error: $err" >&2; exit 1; }
done < <(find "$MACOS" "$FW" -type f ! -path "$MACOS/OrionShell")
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"
echo "  signed    ad-hoc, ${#MACHOS[@]} Mach-O files"

# ── Verify it is self-contained ─────────────────────────────────────────────
#
# Every Mach-O in the bundle, not only the ones this script touched: nothing
# loaded from outside it, and no rpath that searches outside it.
leftover=0
for f in "${MACHOS[@]}"; do
    for dep in $(loads_of "$f") $(rpaths_of "$f"); do
        case "$dep" in
            /opt/homebrew/*|/usr/local/*)
                echo "error: ${f#$APP/} still reaches $dep" >&2; leftover=1 ;;
        esac
    done
done
((leftover == 0)) || exit 1
echo "  verified  no paths outside the bundle, and none searched"

# ── Verify it names nobody ──────────────────────────────────────────────────
#
# The gate of tools/package-app.sh (#266): no home directory of anyone, the
# builder's home and git email by literal.
#
# ⚠ One name excused, in one file: libAvaloniaNative.dylib, prebuilt on NuGet,
# carries /Users/runner/, the GitHub Actions macOS build account that compiled
# it. It names a CI machine, not a person, as /Users/admin/ does in Node.
literal=( -e "$HOME/" )
email="$(git -C "$ROOT" config user.email 2>/dev/null || true)"
if [[ -n "$email" ]]; then literal+=( -e "$email" ); fi
leaks="$( { grep -raoE '/Users/[^/[:space:]]+/' "$APP" | sort -u \
                | grep -vxF "$MACOS/libAvaloniaNative.dylib:/Users/runner/" | cut -d: -f1
            grep -rlaF "${literal[@]}" "$APP"; } | sort -u || true)"
if [[ -n "$leaks" ]]; then
    echo "error: the bundle names a person, in:" >&2
    echo "$leaks" | sed "s|^$APP/|    |" >&2
    exit 1
fi
echo "  verified  no home directory, user name or git email inside"

# ── The disk image ──────────────────────────────────────────────────────────
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"

cat > "$STAGE/README-FIRST.txt" <<TXT
$NAME $LABEL — unofficial alpha build

Orion's cross-platform interface (Avalonia) on the same engine as Orion. It
installs beside Orion.app and keeps its own settings.

Drag $NAME to Applications, then open it ONCE from the right-click menu:

    right-click $NAME.app  ->  Open  ->  Open

macOS blocks it on a normal double-click because this build is signed ad-hoc
rather than with a paid Apple Developer certificate, and is not notarized. It is
the same binary either way; the warning is about who vouched for it, not what it
does. If the dialog gives you no Open button, run:

    xattr -dr com.apple.quarantine "/Applications/$NAME.app"

Requires an Apple silicon Mac running macOS 14 or later. Intel is not built.

This is an alpha. It opens Sony ARW files; other Bayer cameras via LibRaw are
less tested. Edits are saved to XMP sidecars beside your photos and never modify
the raw file.

Third-party licenses are inside the app, at
$NAME.app/Contents/Resources/licenses.
TXT

DMG="$OUT/Orion-$LABEL-avalonia.dmg"
hdiutil create -volname "$NAME $LABEL" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
echo
echo "$DMG"
du -h "$DMG" | cut -f1
