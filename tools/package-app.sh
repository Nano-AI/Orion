#!/bin/bash
#
# Turns the build-tree Orion.app into one a stranger can run.
#
# The development bundle is not redistributable, and none of the reasons are
# obvious from looking at it:
#
#   1. The kernels are not in it. Outside a bundle the engine finds them from
#      the build directory it was built in, so a copied app finds no metallibs
#      and dies on the first open.
#      Same for data/lensfun. src/ResourcePaths.cpp prefers Contents/Resources
#      when it exists, which is what this script fills in.
#   2. It links Homebrew's libraw by absolute path, and libraw pulls libomp,
#      libjpeg and liblcms2 the same way. Four dylibs to carry, with their
#      install names rewritten to @rpath.
#   3. Rewriting a Mach-O invalidates its signature. On Apple silicon an
#      unsigned binary does not launch at all, so it has to be re-signed after
#      install_name_tool, not before.
#   4. The MCP server is TypeScript in mcp/, run by node from the checkout. A
#      downloaded app has neither, so it ships as a Node single-executable
#      application, Contents/MacOS/orion-mcp, which finds the Orion beside it
#      (#310). Fetches the pinned official Node build on first run.
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

# ── Debug map ───────────────────────────────────────────────────────────────
#
# ⚠ The build is RelWithDebInfo, so the binary carries a debug map: an entry per
# object file and source directory, each an absolute path into the checkout,
# which is to say the builder's home directory, forty-eight times over. It is
# there for dsymutil on this machine and no use to anyone the app is handed to.
# `strip -S` drops those entries and keeps the symbol table, so a crash report
# still names functions. Before signing, because stripping invalidates it.
strip -S "$BIN"
echo "  stripped  debug map"

# ── The MCP server ──────────────────────────────────────────────────────────
#
# A Node single-executable application: esbuild bundles mcp/sea.ts and
# everything it imports into one CommonJS file, Node turns that into a blob, and
# postject injects the blob into a copy of the Node binary.
#
# ⚠ **Node, not Bun, and for the license.** Bun's `--compile` was the first
# version and worked, but Bun statically links JavaScriptCore (LGPL-2) and tinycc
# (LGPL-2.1), and CLAUDE.md allows LGPL in proprietary Orion only when it is
# dynamically linked or relinkable. Node and everything it links are permissive.
#
# ⚠ **nodejs.org's build, never Homebrew's.** Homebrew's node links libuv, ICU
# and OpenSSL out of /opt/homebrew, so a copy of it runs only where Homebrew
# does. The official one links system frameworks alone. Pinned by version and
# SHA-256 - the hash nodejs.org publishes in SHASUMS256.txt - so the bytes that
# ship are the bytes that were checked.
#
# `npm ci`, not whatever node_modules happens to hold: the lockfile is what was
# tested. ⚠ Never `strip` this one: the blob lives in a segment of its own.
NODE_VERSION="v24.22.0"
NODE_SHA256="6a9724d9b50a5f22b4816cded441b5a8d9ddae546191a5e5b7efa5a5e2ee26d5"
NODE_CACHE="$BUILD/node-sea"
NODE_TGZ="$NODE_CACHE/node-$NODE_VERSION-darwin-arm64.tar.gz"
NODE="$NODE_CACHE/node-$NODE_VERSION-darwin-arm64/bin/node"
mkdir -p "$NODE_CACHE"
if [[ ! -f "$NODE_TGZ" ]]; then
    curl -sfL -o "$NODE_TGZ.part" \
        "https://nodejs.org/dist/$NODE_VERSION/node-$NODE_VERSION-darwin-arm64.tar.gz"
    mv "$NODE_TGZ.part" "$NODE_TGZ"
fi
echo "$NODE_SHA256  $NODE_TGZ" | shasum -a 256 -c --status \
    || { echo "error: $NODE_TGZ does not match its pinned SHA-256" >&2; exit 1; }
rm -rf "$NODE_CACHE/node-$NODE_VERSION-darwin-arm64"
tar -xzf "$NODE_TGZ" -C "$NODE_CACHE"

MCP="$APP/Contents/MacOS/orion-mcp"
SEA="$NODE_CACHE/sea"
rm -rf "$SEA" && mkdir -p "$SEA"
(cd "$ROOT/mcp" && npm ci --silent --no-audit --no-fund)
# import.meta is empty in CommonJS: dirname becomes the CommonJS one, and url
# undefined, which turns off server.ts's own `node server.ts` entry.
# From mcp/, because the metafile's paths are relative to where esbuild ran and
# tools/mcp-licenses.py reads them against mcp/.
(cd "$ROOT/mcp" && node_modules/.bin/esbuild sea.ts --bundle --platform=node \
    --format=cjs --target=node24 --log-level=warning \
    --define:import.meta.dirname=__dirname --define:import.meta.url=undefined \
    --metafile="$SEA/meta.json" --outfile="$SEA/orion-mcp.cjs")
# ⚠ **Relative paths, run from inside $SEA.** The blob records `main` as
# written, and an absolute one put this checkout's path, home directory and
# all, into every orion-mcp; the scan below is what said so.
cat > "$SEA/sea-config.json" <<JSON
{ "main": "orion-mcp.cjs", "output": "orion-mcp.blob",
  "disableExperimentalSEAWarning": true, "useCodeCache": true }
JSON
(cd "$SEA" && "$NODE" --experimental-sea-config sea-config.json >/dev/null 2>&1) \
    || { echo "error: node could not make the SEA blob" >&2; exit 1; }
cp "$NODE" "$MCP"
chmod u+w "$MCP"
codesign --remove-signature "$MCP"
"$ROOT/mcp/node_modules/.bin/postject" "$MCP" NODE_SEA_BLOB "$SEA/orion-mcp.blob" \
    --sentinel-fuse NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2 \
    --macho-segment-name NODE_SEA >/dev/null
echo "  mcp       orion-mcp $(du -h "$MCP" | cut -f1), node $NODE_VERSION"

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
# orion-mcp is Node, and carries the MCP SDK's npm tree inside its bundle.
cp "$NODE_CACHE/node-$NODE_VERSION-darwin-arm64/LICENSE" "$RES/licenses/node-MIT-and-bundled.txt"
"$ROOT/tools/mcp-licenses.py" "$SEA/meta.json" "$ROOT/mcp" "$RES/licenses/orion-mcp-npm-packages.txt"
cp "$ROOT/NOTICE" "$RES/licenses/NOTICE.txt"
cp "$ROOT/licenses/Apache-2.0.txt" "$RES/licenses/Apache-2.0.txt"   # NOTICE refers to it (Abseil, OpenCV)
echo "  licenses  $(ls "$RES/licenses" | wc -l | tr -d ' ') files"

# ── Sign, after rewriting ───────────────────────────────────────────────────
codesign --remove-signature "$BIN" 2>/dev/null || true
for lib in "$FW"/*.dylib; do codesign --force --sign - "$lib"; done
codesign --force --sign - "$MCP"
codesign --force --deep --sign - "$APP"
codesign --verify --deep "$APP"
echo "  signed    ad-hoc"

# ── Verify it is actually self-contained ────────────────────────────────────
#
# The check that matters. Everything above can succeed and still leave one
# absolute path behind, and the symptom is an app that launches for the person
# who built it and nobody else.
leftover=0
for target in "$BIN" "$MCP" "$FW"/*.dylib; do
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

# ── Verify it names nobody ──────────────────────────────────────────────────
#
# The check above, for a different leak: nothing in the bundle may say who built
# it or where. The 0.5.0-alpha.4 image carried /Users/<name>/... in the binary,
# from the debug map and from resource paths the engine compiled in absolutely,
# and the app looked no different for it (#266). A byte search, not a parse, so
# a path in any file counts.
#
# ⚠ **Anyone's home directory, not only the builder's.** Vendored SwiftTerm
# carried its author's /Users/<name>/Downloads/Logs, which a check for this
# machine's user name passes without a word.
#
# ⚠ **orion-mcp is held to the same rule with one name excused**, and only
# that one. The Node binary inside it carries /Users/admin/ some three thousand
# times, nodejs.org's release machine's paths, which name a build account, not
# a person, and are in every official macOS Node. The builder's home and git
# email are still searched in it like anywhere else.
literal=( -e "$HOME/" )
email="$(git -C "$ROOT" config user.email 2>/dev/null || true)"
if [[ -n "$email" ]]; then literal+=( -e "$email" ); fi
leaks="$( { grep -rlaE --exclude=orion-mcp '/Users/[^/[:space:]]+/' "$APP"
            grep -aoE '/Users/[^/[:space:]]+/' "$MCP" \
                | grep -vxF '/Users/admin/' | sed "s|.*|$MCP|"
            grep -rlaF "${literal[@]}" "$APP"; } | sort -u || true)"
if [[ -n "$leaks" ]]; then
    echo "error: the bundle names a person, in:" >&2
    echo "$leaks" | sed "s|^$APP/|    |" >&2
    exit 1
fi
echo "  verified  no home directory, user name or git email inside"

# ── Verify the packaged MCP server drives the packaged binary ───────────────
#
# Every step of check-agent, against this bundle's orion-mcp with ORION_BIN
# unset: a real render, a real approve, and the sibling lookup that makes a
# downloaded app need no configuration. The `.ts` passing in the checkout says
# nothing about what was bundled.
"$ROOT/tools/check-agent.py" --server "$MCP" >/dev/null \
    || { echo "error: check-agent --server failed on the packaged orion-mcp" >&2; exit 1; }
echo "  verified  check-agent green against the packaged orion-mcp"

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

Claude and other AI agents can edit through Orion's MCP server, which is inside
the app. Nothing is saved to your photos until you approve it in Orion.

macOS will not run the server for an agent while the download's quarantine is
on it, even after you have opened Orion, so with Orion in Applications run:

    xattr -dr com.apple.quarantine /Applications/Orion.app

Then, for Claude Code:

    claude mcp add orion -- /Applications/Orion.app/Contents/MacOS/orion-mcp

For Claude Desktop, add this under "mcpServers" in its config file:

    "orion": { "command": "/Applications/Orion.app/Contents/MacOS/orion-mcp" }

Orion's own Assistant panel (Assistant menu) loads it with no setup, and offers
an Allow button if the quarantine is still on. Assistant > Copy MCP Setup
Command copies both lines above.

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
