#!/bin/bash
#
# Turns the build-tree Orion.app into one a stranger can run.
#
# The development bundle is not redistributable, and none of the reasons are
# obvious from looking at it:
#
#   1. The kernels are not in it. ORION_SHADER_DIR is an absolute path into this
#      build tree, so a copied app finds no metallibs and dies on the first open.
#      Same for data/lensfun. src/ResourcePaths.cpp prefers Contents/Resources
#      when it exists, which is what this script fills in.
#   2. It links Homebrew's libraw by absolute path, and libraw pulls libomp,
#      libjpeg and liblcms2 the same way. Four dylibs to carry, with their
#      install names rewritten to @rpath.
#   3. Rewriting a Mach-O invalidates its signature. On Apple silicon an
#      unsigned binary does not launch at all, so it has to be re-signed after
#      install_name_tool, not before.
#
# Usage:  tools/package-app.sh [output-dir] [version-label]
#
# The label goes in the disk image's name and its readme. It exists because the
# plist carries 0.4.0 for both alpha.1 and alpha.2 — the prerelease suffix lives
# in the git tag — and two different builds arriving as Orion-0.4.0.dmg is how
# somebody ends up debugging the wrong binary.
#
# Produces a .dmg and leaves the staged .app beside it. Ad-hoc signed, not
# notarized: first launch needs the quarantine flag cleared, and README-FIRST.txt
# inside the disk image says how.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
OUT="${1:-$ROOT/dist}"
LABEL="${2:-}"

APP_SRC="$BUILD/Orion.app"
APP="$OUT/Orion.app"

[[ -d "$APP_SRC" ]] || { echo "error: $APP_SRC missing — build first" >&2; exit 1; }
[[ -d "$BUILD/shaders" ]] || { echo "error: $BUILD/shaders missing" >&2; exit 1; }

VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
           "$APP_SRC/Contents/Info.plist")"
NAME="${LABEL:-$VERSION}"
echo "Orion $NAME"

rm -rf "$OUT"
mkdir -p "$OUT"
cp -R "$APP_SRC" "$APP"

RES="$APP/Contents/Resources"
FW="$APP/Contents/Frameworks"
mkdir -p "$RES" "$FW"

# ── Resources ───────────────────────────────────────────────────────────────
echo "  shaders   $(ls "$BUILD/shaders" | wc -l | tr -d ' ') metallibs"
cp -R "$BUILD/shaders" "$RES/shaders"

echo "  data      lensfun"
mkdir -p "$RES/data"
cp -R "$ROOT/data/lensfun" "$RES/data/lensfun"

# ── Dylibs ──────────────────────────────────────────────────────────────────
#
# Walk the dependency graph rather than listing the four by name: libraw's own
# dependencies are a property of how Homebrew built it, and a list here would go
# stale silently the next time it is rebuilt with one more of them.
BIN="$APP/Contents/MacOS/Orion"

collect() {
    local target="$1"
    otool -L "$target" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        case "$dep" in
            /opt/homebrew/*|/usr/local/*) echo "$dep" ;;
        esac
    done
}

# A space-delimited seen list rather than an associative array: macOS ships bash
# 3.2, where `declare -A` does not exist. The queue is small enough that a linear
# membership test costs nothing.
QUEUE="$(collect "$BIN" | tr '\n' ' ')"
SEEN=" "

while [[ -n "${QUEUE// /}" ]]; do
    set -- $QUEUE
    dep="$1"; shift; QUEUE="$*"
    base="$(basename "$dep")"
    case "$SEEN" in *" $base "*) continue ;; esac
    SEEN="$SEEN$base "

    [[ -f "$dep" ]] || { echo "error: $dep not found" >&2; exit 1; }
    cp -f "$dep" "$FW/$base"
    chmod u+w "$FW/$base"
    echo "  dylib     $base"

    QUEUE="$QUEUE $(collect "$FW/$base" | tr '\n' ' ')"
done

# Rewrite every reference, in the binary and in the copied dylibs.
retarget() {
    local target="$1"
    otool -L "$target" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        case "$dep" in
            /opt/homebrew/*|/usr/local/*)
                install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$target"
                ;;
        esac
    done
}

retarget "$BIN"
for lib in "$FW"/*.dylib; do
    install_name_tool -id "@rpath/$(basename "$lib")" "$lib"
    retarget "$lib"
done

# The build already carries @executable_path/../Frameworks. Drop the Homebrew
# rpath so a machine that happens to have a different libraw installed cannot
# load that one instead of the one shipped here.
install_name_tool -delete_rpath /opt/homebrew/opt/libraw/lib "$BIN" 2>/dev/null || true

# ── Licenses ────────────────────────────────────────────────────────────────
#
# Copied verbatim from the installed packages, never retyped: LibRaw is
# LGPL-2.1, which requires its terms to travel with the binary.
mkdir -p "$RES/licenses"
copy_license() {
    local name="$1" path="$2"
    if [[ -f "$path" ]]; then
        cp "$path" "$RES/licenses/$name"
    else
        echo "warning: license for $name not found at $path" >&2
    fi
}
LIBRAW_DIR="$(dirname "$(dirname "$(readlink -f /opt/homebrew/opt/libraw/lib/libraw_r.dylib)")")"
copy_license "LibRaw-LGPL-2.1.txt" "$LIBRAW_DIR/LICENSE.LGPL"
copy_license "LibRaw-CDDL-1.0.txt" "$LIBRAW_DIR/LICENSE.CDDL"
copy_license "LibRaw-COPYRIGHT.txt" "$LIBRAW_DIR/COPYRIGHT"
for spec in \
    "libomp-Apache-2.0-LLVM.txt:libomp:LICENSE.TXT" \
    "libjpeg-turbo.md:jpeg-turbo:LICENSE.md" \
    "little-cms2-MIT.txt:little-cms2:LICENSE"
do
    IFS=: read -r out pkg file <<< "$spec"
    src="$(ls -d /opt/homebrew/Cellar/"$pkg"/*/"$file" 2>/dev/null | tail -1 || true)"
    copy_license "$out" "${src:-/nonexistent}"
done
cp "$ROOT/NOTICE" "$RES/licenses/NOTICE.txt"
echo "  licenses  $(ls "$RES/licenses" | wc -l | tr -d ' ') files"

# ── Sign, after rewriting ───────────────────────────────────────────────────
codesign --remove-signature "$BIN" 2>/dev/null || true
for lib in "$FW"/*.dylib; do codesign --force --sign - "$lib"; done
codesign --force --deep --sign - "$APP"
codesign --verify --deep "$APP"
echo "  signed    ad-hoc"

# ── Verify it is actually self-contained ────────────────────────────────────
#
# The check that matters. Everything above can succeed and still leave one
# absolute path behind, and the symptom is an app that launches for the person
# who built it and nobody else.
leftover=0
for target in "$BIN" "$FW"/*.dylib; do
    while read -r dep; do
        case "$dep" in
            /opt/homebrew/*|/usr/local/*)
                echo "error: $(basename "$target") still references $dep" >&2
                leftover=1 ;;
        esac
    done < <(otool -L "$target" | tail -n +2 | awk '{print $1}')
done
((leftover == 0)) || exit 1
echo "  verified  no paths outside the bundle"

# ── The disk image ──────────────────────────────────────────────────────────
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP" "$STAGE/Orion.app"
ln -s /Applications "$STAGE/Applications"

cat > "$STAGE/README-FIRST.txt" <<TXT
Orion $NAME — unofficial alpha build

Drag Orion to Applications, then open it ONCE from the right-click menu:

    right-click Orion.app  ->  Open  ->  Open

macOS blocks it on a normal double-click because this build is signed ad-hoc
rather than with a paid Apple Developer certificate, and is not notarized. It is
the same binary either way; the warning is about who vouched for it, not what it
does. If the dialog gives you no Open button, run:

    xattr -dr com.apple.quarantine /Applications/Orion.app

Requires an Apple silicon Mac running macOS 14 or later. Intel is not built.

This is an alpha. It opens Sony ARW files; other Bayer cameras via LibRaw are
less tested. Edits are saved to XMP sidecars beside your photos and never modify
the raw file.

Third-party licenses are inside the app, at
Orion.app/Contents/Resources/licenses.
TXT

DMG="$OUT/Orion-$NAME.dmg"
rm -f "$DMG"
hdiutil create -volname "Orion $NAME" -srcfolder "$STAGE" \
               -ov -format UDZO "$DMG" >/dev/null
echo
echo "$DMG"
du -h "$DMG" | cut -f1
