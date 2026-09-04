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

# The LC_RPATH list of one Mach-O, absolute entries only.
rpaths_of() {
    otool -l "$1" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}' | grep -v '^@' || true
}

# Every dependency of one Mach-O that lives outside the bundle, absolute, one
# per line.
#
# ⚠ **A dependency written `@rpath/libfoo.dylib` is still a dependency**, and
# this collected only absolute Homebrew paths at first. Homebrew's OpenCV names
# its own siblings that way — `libopencv_video` asks for `@rpath/libopencv_dnn`,
# which asks for protobuf, absl and OpenVINO — so the bundle carried the six
# libraries the binary named outright and none of the tree beneath them. It
# looked complete because the app still ran: a Cellar directory was left on the
# binary's rpath and quietly caught every miss. Take that away, as this script
# now does, and the app dies at launch on a library nobody copied.
#
# ⚠ `origin` is the other half and is easy to leave out. `@loader_path` means
# *beside the referring file*, and by the time the walk reads a library it is
# already a copy in Frameworks/, where the thing it wants has not been copied
# yet and may never be. `libgfortran` asks for `@rpath/libgcc_s.1.1.dylib` with
# `@loader_path` as its only rpath: resolving against the copy finds nothing,
# resolving against the directory it came from finds it. So the caller says
# where each library was taken from.
#
# ⚠ **The rpath list is read once per file, not once per dependency.** It was
# the latter first, and the walk went from seconds to minutes without ever
# failing — `otool -l` dumps every load command, OpenCV's dnn module is 13 MB
# with some forty dependencies, and it was re-dumped for each one. Slow enough
# to look like a hang, which is how it was noticed.
collect() {
    local target="$1" origin="${2:-}" dep name rp hit rps
    rps="$(rpaths_of "$target")"

    for dep in $(otool -L "$target" | tail -n +2 | awk '{print $1}'); do
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

    # ⚠ Explicit: the loop's status is the last `if`, and an unresolved final
    # dependency would make this function "fail" under `set -e`, killing the
    # script from inside the command substitution that calls it.
    return 0
}

# A space-delimited seen list rather than an associative array: macOS ships bash
# 3.2, where `declare -A` does not exist.
#
# ⚠ **Nothing already seen is ever put on the queue**, and the difference is not
# tidiness. The first version appended every dependency and skipped duplicates
# when they came back off, which is fine for the ten libraries LibRaw needed and
# quadratic in the ninety-eight that OpenCV's dnn module drags in: each of the
# ~30 dependencies of each library went on, so the queue grew to thousands of
# entries and every single iteration re-split and re-joined the whole string.
# The walk sat at 100% CPU with no child processes for eleven minutes and looked
# exactly like a hang — it was arithmetic. Marking seen at *enqueue* time bounds
# the queue by the number of distinct libraries.
#
# `${path##*/}` rather than `basename` for the same reason: one fork per
# dependency is not free at this size.
SEEN=" "
PENDING=""

enqueue() {
    local path base
    for path in "$@"; do
        base="${path##*/}"
        case "$SEEN" in *" $base "*) continue ;; esac
        SEEN="$SEEN$base "
        # ⚠ Leading space, not trailing. `PENDING="$*"` below rejoins without
        # one, so appending `"$PENDING$path "` glued the next path onto the last
        # and the walk asked for a file whose name was two paths concatenated.
        PENDING="$PENDING $path"
    done
}

enqueue $(collect "$BIN")

while [[ -n "${PENDING// /}" ]]; do
    set -- $PENDING
    dep="$1"; shift; PENDING="$*"
    base="${dep##*/}"

    [[ -f "$dep" ]] || { echo "error: $dep not found" >&2; exit 1; }
    cp -f "$dep" "$FW/$base"
    chmod u+w "$FW/$base"
    echo "  dylib     $base"

    enqueue $(collect "$FW/$base" "$(dirname "$dep")")
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

# ── Runtime search paths ────────────────────────────────────────────────────
#
# ⚠ **Rewriting the load commands is only half the job, and `otool -L` cannot
# see the other half.** Every reference now reads `@rpath/libfoo.dylib` — but
# `@rpath` is a *list*, searched in order, and the build leaves Homebrew
# directories on it. `/opt/homebrew/opt/opencv/lib` sat ahead of
# `@executable_path/../Frameworks`, so the shipped app loaded five OpenCV
# dylibs out of the Cellar and never touched the copies beside them. Those then
# pulled a second libomp and the app died on the first raw it decoded:
# `OMP: Error #15 ... libomp.dylib already initialized`. One file, two paths,
# two OpenMP runtimes.
#
# ⚠ It shipped that way because the rule here named one library:
# `-delete_rpath /opt/homebrew/opt/libraw/lib`. OpenCV joined the stack later
# and nothing told this script. So it is a sweep now, the same shape as the
# dependency walk above and for the same reason — a list of names goes stale in
# silence, and the failure it produces is "works for the person who built it".
#
# `@loader_path` and `@executable_path` entries are left alone: those resolve
# inside the bundle, which is the point of them.
strip_rpaths() {
    local target="$1" rp
    for rp in $(rpaths_of "$target"); do
        case "$rp" in
            /opt/homebrew/*|/usr/local/*)
                install_name_tool -delete_rpath "$rp" "$target" 2>/dev/null || true ;;
        esac
    done
}

strip_rpaths "$BIN"
for lib in "$FW"/*.dylib; do strip_rpaths "$lib"; done
echo "  rpaths    Homebrew search paths removed"

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

    # ⚠ **The rpaths, and it did not always.** This block printed "verified no
    # paths outside the bundle" on a bundle that loaded its OpenCV out of
    # /opt/homebrew and aborted on the first photograph — `otool -L` shows what
    # is referenced and says nothing about where `@rpath` will look for it. A
    # check that inspects half the mechanism reports success for exactly the
    # failure it exists to catch.
    while read -r rp; do
        case "$rp" in
            /opt/homebrew/*|/usr/local/*)
                echo "error: $(basename "$target") still searches $rp" >&2
                leftover=1 ;;
        esac
    done < <(rpaths_of "$target")
done
((leftover == 0)) || exit 1
echo "  verified  no paths outside the bundle, and none searched"

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
