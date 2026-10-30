#!/bin/bash
#
# Packages Orion (the Avalonia app in shell/) as a Mac app a stranger can run:
# Orion.app and Orion-<label>.dmg, ad-hoc signed, not notarized, and the
# Velopack release an installed Orion updates from (releases/, #389).
#
# Until decision #380 this file packaged the SwiftUI app, and this script was
# tools/package-avalonia.sh beside it. The Swift script solved most of these
# problems first, and its ⚠ notes, kept where they still apply, are why the
# steps look the way they do: `git log -- tools/package-app.sh` before #380.
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
#   3. The Swift app's identity, so an update is an update: Orion.app,
#      com.nanoai.orion, the executable `Orion`, the version CMake carries, and
#      the disk image name the download page and the releases use. macOS keys
#      defaults, window state and permissions by bundle id, and the app reads
#      the Swift app's Application Support folder (SettingsImport).
#   4. The MCP server ships inside, as Contents/MacOS/orion-mcp, a Node
#      single-executable application that runs the `Orion` beside it (#310),
#      and every step of tools/check-agent.py runs against it before the disk
#      image is made.
#
# Usage:  tools/package-app.sh [output-dir] [label]
#         label defaults to `git describe --tags`, and goes in the image's name
#         (Orion-<label>.dmg) and its readme: the plist carries 0.6.0 for every
#         alpha, and two builds arriving as one name is how somebody debugs the
#         wrong binary.
# Needs:  cmake + Xcode's Metal toolchain, Homebrew libraw and opencv, dotnet 10
#         (DOTNET_ROOT defaults to Homebrew's), node and npm, and a sample RAW
#         for the check-agent run (ORION_SAMPLES, see tools/check-agent.py),
#         and vpk (`dotnet tool install -g vpk`, the version build.yml pins;
#         or $VPK). The label must be SemVer once its v is dropped.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
SRC="$ROOT/shell"
OUT="${1:-$ROOT/dist}"
LABEL="${2:-$(git -C "$ROOT" describe --tags)}"
NAME="Orion"
BUNDLE_ID="com.nanoai.orion"
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
dotnet publish "$SRC/Orion.csproj" -c Release -r osx-arm64 --self-contained \
    -p:DebugType=None -p:DebugSymbols=false --nologo -v quiet -o "$MACOS"
echo "  publish   $(ls "$MACOS" | wc -l | tr -d ' ') files, .NET $(dotnet --version)"

# ── Resources ───────────────────────────────────────────────────────────────
mkdir -p "$RES/data" "$FW"
cp -R "$BUILD/shaders" "$RES/shaders"
cp -R "$ROOT/data/lensfun" "$RES/data/lensfun"
echo "  shaders   $(ls "$RES/shaders" | wc -l | tr -d ' ') metallibs, data lensfun"

# ── The MCP server ──────────────────────────────────────────────────────────
#
# A Node single-executable application: esbuild bundles mcp/sea.ts and
# everything it imports into one CommonJS file, Node turns that into a blob, and
# postject injects the blob into a copy of the Node binary.
#
# ⚠ **Node, not Bun, and for the license.** Bun's `--compile` statically links
# JavaScriptCore (LGPL-2) and tinycc (LGPL-2.1), and CLAUDE.md allows LGPL only
# when it is dynamically linked or relinkable. Node and everything it links are
# permissive.
#
# ⚠ **nodejs.org's build, never Homebrew's.** Homebrew's node links libuv, ICU
# and OpenSSL out of /opt/homebrew, so a copy of it runs only where Homebrew
# does. The official one links system frameworks alone. Pinned by version and
# SHA-256 - the hash nodejs.org publishes in SHASUMS256.txt.
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

MCP="$MACOS/orion-mcp"
SEA="$NODE_CACHE/sea"
rm -rf "$SEA" && mkdir -p "$SEA"
(cd "$ROOT/mcp" && npm ci --silent --no-audit --no-fund)
# import.meta is empty in CommonJS: dirname becomes the CommonJS one, and url
# undefined, which turns off server.ts's own `node server.ts` entry. From mcp/,
# because tools/mcp-licenses.py reads the metafile's paths against mcp/.
(cd "$ROOT/mcp" && node_modules/.bin/esbuild sea.ts --bundle --platform=node \
    --format=cjs --target=node24 --log-level=warning \
    --define:import.meta.dirname=__dirname --define:import.meta.url=undefined \
    --metafile="$SEA/meta.json" --outfile="$SEA/orion-mcp.cjs")
# ⚠ **Relative paths, run from inside $SEA.** The blob records `main` as
# written, and an absolute one put the checkout's path, home directory and all,
# into every orion-mcp; the scan below is what said so.
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

VERSION="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD/CMakeCache.txt")"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>$NAME</string>
    <key>CFBundleDisplayName</key><string>$NAME</string>
    <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
    <key>CFBundleExecutable</key><string>Orion</string>
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
# Copied verbatim from the installed packages, as the Swift app's packaging did, and
# ⚠ **a licence that is not found stops the build.** It was a warning, which
# ships a bundle without the text its licence requires and says so in a line
# nobody reads. NOTICE names every component; `licence_of` below maps every
# file in the finished bundle to the licence files it needs, after the dylib
# walk, so a component that arrives with a new dependency fails here rather
# than shipping unlicensed.
mkdir -p "$RES/licenses"
copy_license() {
    [[ -f "$2" ]] || { echo "error: license for $1 not found at $2" >&2; exit 1; }
    cp "$2" "$RES/licenses/$1"
}
cellar() { ls -d /opt/homebrew/Cellar/"$1"/*/$2 2>/dev/null | tail -1 || true; }
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
    "brotli-MIT.txt:brotli:LICENSE" \
    "abseil-Apache-2.0.txt:abseil:LICENSE" \
    "openvino-Apache-2.0.txt:openvino:LICENSE" \
    "oneTBB-Apache-2.0.txt:tbb:LICENSE.txt" \
    "protobuf-BSD-3-Clause.txt:protobuf:LICENSE" \
    "openblas-BSD-3-Clause.txt:openblas:LICENSE" \
    "pugixml-MIT.md:pugixml:LICENSE.md" \
    "gcc-COPYING.RUNTIME.txt:gcc:COPYING.RUNTIME" \
    "gcc-libquadmath-LGPL-2.1.txt:gcc:COPYING.LIB"
do
    IFS=: read -r out pkg file <<< "$spec"
    src="$(cellar "$pkg" "$file")"
    copy_license "$out" "${src:-/nonexistent/$pkg/$file}"
done
# GCC ships GPL-3.0 only as its gpl(7) page; libgcc_s and libgfortran are under
# it, with the runtime exception above.
GPL7="$(cellar gcc 'share/man/man7/gpl-*.7')"
[[ -f "$GPL7" ]] || { echo "error: GCC's gpl(7) page not found" >&2; exit 1; }
mandoc -T utf8 "$GPL7" | col -b > "$RES/licenses/gcc-GPL-3.0.txt"

# The .NET runtime, the framework and the assistant terminal's NuGet packages.
# NOTICE carries the MIT text for each; a package that ships a licence or a
# notices file gets it copied verbatim too. Versions from packages.lock.json,
# so a bumped package is a missing file here rather than an old text.
NUGET="${NUGET_PACKAGES:-$HOME/.nuget/packages}"
locked() { python3 -c 'import json,sys; d=json.load(open(sys.argv[1]))["dependencies"]; print(next(v["resolved"] for t in d.values() for k,v in t.items() if k==sys.argv[2]))' "$SRC/packages.lock.json" "$1"; }
SKIA="$(locked SkiaSharp)"; HB="$(locked HarfBuzzSharp)"
copy_license "SkiaSharp-MIT.txt" "$NUGET/skiasharp/$SKIA/LICENSE.txt"
copy_license "libSkiaSharp-THIRD-PARTY-NOTICES.txt" "$NUGET/skiasharp.nativeassets.macos/$SKIA/THIRD-PARTY-NOTICES.txt"
copy_license "HarfBuzzSharp-MIT.txt" "$NUGET/harfbuzzsharp/$HB/LICENSE.txt"
copy_license "libHarfBuzzSharp-THIRD-PARTY-NOTICES.txt" "$NUGET/harfbuzzsharp.nativeassets.macos/$HB/THIRD-PARTY-NOTICES.txt"
copy_license "Unicode.net-MIT.txt" "$NUGET/unicode.net/$(locked Unicode.net)/LICENSE"
copy_license "SkiaSharp.HarfBuzz-MIT.txt" "$NUGET/skiasharp.harfbuzz/$(locked SkiaSharp.HarfBuzz)/LICENSE.txt"
# dotnet/runtime's own notices. Homebrew's source-built SDK unpacks the runtime
# pack without them; the ILCompiler package beside it is built from the same
# repository and carries the file. Microsoft's SDK (CI's, #389) has it at its root,
# where tools/package-gates.py reads it.
ILC="$(ls "$DOTNET_ROOT"/library-packs/Microsoft.DotNet.ILCompiler.*.nupkg 2>/dev/null | tail -1 || true)"
if [[ -f "$ILC" ]]; then unzip -p "$ILC" THIRD-PARTY-NOTICES.TXT > "$RES/licenses/dotnet-runtime-THIRD-PARTY-NOTICES.txt"
else ILC="$DOTNET_ROOT/ThirdPartyNotices.txt"; cp "$ILC" "$RES/licenses/dotnet-runtime-THIRD-PARTY-NOTICES.txt" 2>/dev/null || true; fi
[[ -s "$RES/licenses/dotnet-runtime-THIRD-PARTY-NOTICES.txt" ]] || { echo "error: $ILC has no THIRD-PARTY-NOTICES.TXT" >&2; exit 1; }
copy_license "OFL-1.1.txt" "$SRC/Assets/Fonts/OFL.txt"   # Barlow, and Inter inside Avalonia.Fonts.Inter
# orion-mcp is Node, and carries the MCP SDK's npm tree inside its bundle.
copy_license "node-MIT-and-bundled.txt" "$NODE_CACHE/node-$NODE_VERSION-darwin-arm64/LICENSE"
"$ROOT/tools/mcp-licenses.py" "$SEA/meta.json" "$ROOT/mcp" "$RES/licenses/orion-mcp-npm-packages.txt"
cp "$ROOT/NOTICE" "$RES/licenses/NOTICE.txt"
cp "$ROOT/licenses/Apache-2.0.txt" "$RES/licenses/Apache-2.0.txt"   # NOTICE refers to it (Abseil, OpenCV)
# Dawn (#375) is linked statically into the shim when the engine archive has
# it (native/build.sh decides the same way), so no file in Frameworks names it
# and the per-file check below cannot see it: its notices are NOTICE's Dawn
# section, required here, and Dawn's own LICENSE when the source tree is kept.
members=$(ar t "$BUILD/engine/liborion_engine.a")   # not piped into grep -q: pipefail and SIGPIPE
if [[ $'\n'$members$'\n' == *$'\n'DawnDevice.cpp.o$'\n'* ]]; then
    grep -q '^## Dawn (WebGPU)$' "$ROOT/NOTICE" || { echo "error: the shim links Dawn and NOTICE has no Dawn section" >&2; exit 1; }
    DAWN_LICENSE="$ROOT/third_party/dawn-src/LICENSE"
    [[ -f "$DAWN_LICENSE" ]] && copy_license "dawn-BSD-3-Clause.txt" "$DAWN_LICENSE"
    echo "  dawn      linked into the shim; notices from NOTICE"
fi
echo "  licenses  $(ls "$RES/licenses" | wc -l | tr -d ' ') files"

# ── Dylibs ──────────────────────────────────────────────────────────────────
#
# The Swift app's dependency walk, started from every Mach-O in
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

# ── Verify every file is licensed ───────────────────────────────────────────
#
# Each file in Contents/MacOS and Contents/Frameworks, mapped to the licence
# files it needs. ⚠ A file no pattern knows is an error, not a pass: the walk
# above adds whatever Homebrew's OpenCV links this month, and the bundle once
# carried thirty-odd libraries NOTICE had never named (Abseil, OpenVINO,
# oneTBB, protobuf, OpenBLAS, the GCC runtime). A list of names goes stale in
# silence, the dependency walk's own lesson.
licence_of() {
    case "$1" in
        Orion|OrionShell|Orion.dll|OrionShell.dll|*.deps.json|*.runtimeconfig.json|liborion_shim.dylib)
                                    echo NOTICE.txt ;;   # Orion's own, and the apphost's .NET MIT
        System.*|Microsoft.*|mscorlib.dll|netstandard.dll|WindowsBase.dll|createdump|libSystem.*.dylib|\
        libcoreclr.dylib|libclrjit.dylib|libclrgc*.dylib|libhostfxr.dylib|libhostpolicy.dylib|libmscordaccore.dylib|libmscordbi.dylib)
                                    echo NOTICE.txt dotnet-runtime-THIRD-PARTY-NOTICES.txt ;;
        Avalonia.Fonts.Inter.dll)   echo NOTICE.txt OFL-1.1.txt ;;
        Avalonia*.dll|libAvaloniaNative.dylib|MicroCom.Runtime.dll|Tmds.DBus.Protocol.dll)
                                    echo NOTICE.txt ;;
        SkiaSharp.HarfBuzz.dll)     echo SkiaSharp.HarfBuzz-MIT.txt ;;
        SkiaSharp.dll|libSkiaSharp.dylib) echo SkiaSharp-MIT.txt libSkiaSharp-THIRD-PARTY-NOTICES.txt ;;
        HarfBuzzSharp.dll|libHarfBuzzSharp.dylib) echo HarfBuzzSharp-MIT.txt libHarfBuzzSharp-THIRD-PARTY-NOTICES.txt ;;
        Iciclecreek.Avalonia.Terminal.dll|XTerm.NET.dll|Porta.Pty.dll|libporta_pty.dylib|Wcwidth.dll|Velopack.dll)
                                    echo NOTICE.txt ;;
        NeoSmart.Unicode.dll)       echo Unicode.net-MIT.txt ;;
        orion-mcp)                  echo node-MIT-and-bundled.txt orion-mcp-npm-packages.txt ;;
        libraw_r.*)                 echo LibRaw-LGPL-2.1.txt LibRaw-CDDL-1.0.txt LibRaw-COPYRIGHT.txt ;;
        libomp.*)                   echo libomp-Apache-2.0-LLVM.txt ;;
        libjpeg.*)                  echo libjpeg-turbo.md ;;
        liblcms2.*)                 echo little-cms2-MIT.txt ;;
        libopencv_*)                echo opencv-Apache-2.0.txt ;;
        libjxl*)                    echo libjxl-BSD-3-Clause.txt ;;
        libhwy.*)                   echo highway-Apache-2.0-or-BSD-3-Clause.txt ;;
        libbrotli*)                 echo brotli-MIT.txt ;;
        libabsl_*)                  echo abseil-Apache-2.0.txt Apache-2.0.txt ;;
        libopenvino*)               echo openvino-Apache-2.0.txt ;;
        libtbb*)                    echo oneTBB-Apache-2.0.txt ;;
        libprotobuf*|libutf8_validity*) echo protobuf-BSD-3-Clause.txt ;;
        libopenblas*)               echo openblas-BSD-3-Clause.txt ;;
        libpugixml*)                echo pugixml-MIT.md ;;
        libgcc_s.*|libgfortran.*)   echo gcc-COPYING.RUNTIME.txt gcc-GPL-3.0.txt ;;
        libquadmath.*)              echo gcc-libquadmath-LGPL-2.1.txt ;;
    esac
}
unlicensed=0
while IFS= read -r f; do
    need="$(licence_of "${f##*/}")"
    [[ -n "$need" ]] || { echo "error: ${f#$APP/} has no licence mapping; name it in NOTICE and licence_of" >&2; unlicensed=1; continue; }
    for l in $need; do
        [[ -f "$RES/licenses/$l" ]] || { echo "error: ${f#$APP/} needs licenses/$l, which is not there" >&2; unlicensed=1; }
    done
done < <(find "$MACOS" "$FW" -type f)
((unlicensed == 0)) || exit 1
echo "  verified  every file in MacOS and Frameworks maps to its licence"

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
find_machos() {
    MACHOS=()
    while IFS= read -r f; do
        if file -b "$f" | grep -q Mach-O; then MACHOS+=("$f"); fi
    done < <(find "$MACOS" "$FW" -type f)
}
find_machos
while IFS= read -r f; do
    err="$(codesign --force --sign - "$f" 2>&1)" || { echo "error: $err" >&2; exit 1; }
done < <(find "$MACOS" "$FW" -type f ! -path "$MACOS/Orion")
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"
echo "  signed    ad-hoc, ${#MACHOS[@]} Mach-O files"

# ── Velopack: the update feed, and the app that can update itself ──────────
#
# vpk adds UpdateMac and sq.version to a copy of the bundle, signs it again
# and writes the release into $OUT/releases (#389): Orion-<v>-osx-full.nupkg
# and releases.osx.json are what an installed Orion reads from GitHub Releases.
# The app the disk image carries is that copy, unzipped over $APP, so every
# check below runs on the bundle that ships and a dragged-in Orion updates.
#
# ⚠ **`--signAppIdentity -` is ad-hoc**, the same signature as above: without
# an identity vpk signs nothing and leaves the bundle's seal broken by its own
# two files. `--signDisableDeep` keeps the per-file signatures made above; a
# Developer ID later goes here, with --notaryProfile (decision #389). The
# apphost gets the hardened runtime and vpk's default .NET entitlements (JIT,
# no library validation). `--noInst`: no .pkg; the disk image is the first install.
# ⚠ The .nupkg an update installs drops xattrs, so the managed .dlls' signatures
# above (see Sign) are lost and an updated bundle fails `codesign --verify`. It
# runs, unquarantined; a notarized build must fix it first (#389).
VPK_VERSION="${LABEL#v}"   # the tag without its v: v0.5.0-alpha.6 is 0.5.0-alpha.6, SemVer as vpk needs
"${VPK:-vpk}" pack --packId "$NAME" --packVersion "$VPK_VERSION" --packDir "$APP" --mainExe Orion \
    --signAppIdentity - --signDisableDeep --noInst --outputDir "$OUT/releases" >/dev/null
rm -rf "$APP"
ditto -x -k "$OUT/releases/$NAME-osx-Portable.zip" "$OUT"   # ditto: the .dlls' signatures live in xattrs
codesign --verify --deep --strict "$APP"
find_machos   # again: vpk adds UpdateMac and leaves out createdump
echo "  velopack  $VPK_VERSION, $(ls "$OUT/releases" | wc -l | tr -d ' ') files in releases/"

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
# The Swift app's personal-data gate (#266): no home directory of anyone, the
# builder's home and git email by literal.
#
# ⚠ **Anyone's home directory, not only the builder's**: vendored code once
# carried its author's /Users/<name>/Downloads/Logs, which a check for this
# machine's user name passes without a word.
#
# ⚠ Three names excused, each in one file. libAvaloniaNative.dylib, prebuilt on
# NuGet, and Velopack's UpdateMac carry /Users/runner/, the GitHub Actions macOS
# build account that compiled them; orion-mcp carries /Users/admin/ some three thousand times,
# nodejs.org's release machine's paths, in every official macOS Node. Both name
# a build account, not a person. The builder's home and git email are still
# searched in both like anywhere else, except on CI (#389), where the home is
# that build account, as tools/package-gates.py has it.
literal=( -e "$HOME/" )
if [[ -n "${CI:-}" ]]; then literal=( -e "/nonexistent-home/" ); fi
email="$(git -C "$ROOT" config user.email 2>/dev/null || true)"
if [[ -n "$email" ]]; then literal+=( -e "$email" ); fi
leaks="$( { grep -raoE '/Users/[^/[:space:]]+/' "$APP" | sort -u \
                | grep -vxF -e "$MACOS/libAvaloniaNative.dylib:/Users/runner/" -e "$MACOS/UpdateMac:/Users/runner/" -e "$MCP:/Users/admin/" | cut -d: -f1
            grep -rlaF "${literal[@]}" "$APP"; } | sort -u || true)"
if [[ -n "$leaks" ]]; then
    echo "error: the bundle names a person, in:" >&2
    echo "$leaks" | sed "s|^$APP/|    |" >&2
    exit 1
fi
echo "  verified  no home directory, user name or git email inside"

# ── Verify the packaged MCP server drives the packaged app ─────────────────
#
# Every step of check-agent, against this bundle's orion-mcp with ORION_BIN
# unset: a real render, a real approve, and the sibling lookup that makes a
# downloaded app need no configuration. The `.ts` passing in the checkout says
# nothing about what was bundled.
# ⚠ A stated skip on CI only, which has no sample RAW (#389); anywhere else a
# missing sample still fails the build.
if [[ -n "${CI:-}" && -z "${ORION_SAMPLES:-}" ]]; then
    echo "  skipped   check-agent: no sample RAW on CI (stated skip, #389)"
else
    "$ROOT/tools/check-agent.py" --server "$MCP" >/dev/null \
        || { echo "error: check-agent --server failed on the packaged orion-mcp" >&2; exit 1; }
    echo "  verified  check-agent green against the packaged orion-mcp"
fi

# ── The disk image ──────────────────────────────────────────────────────────
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"

cat > "$STAGE/README-FIRST.txt" <<TXT
Orion $LABEL — unofficial alpha build

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
the raw file. It replaces the earlier Orion.app in place and keeps its presets,
watermark and assistant settings.

Claude and other AI agents can edit through Orion's MCP server, which is inside
the app. Nothing is saved to your photos until you approve it in Orion.

macOS will not run the server for an agent while the download's quarantine is
on it, even after you have opened Orion, so with Orion in Applications run:

    xattr -dr com.apple.quarantine /Applications/Orion.app

Then, for Claude Code:

    claude mcp add orion -- /Applications/Orion.app/Contents/MacOS/orion-mcp

For Claude Desktop, add this under "mcpServers" in its config file:

    "orion": { "command": "/Applications/Orion.app/Contents/MacOS/orion-mcp" }

Orion's own Assistant column (Command-Option-A) loads it with no setup, and
offers an Allow button if the quarantine is still on.

Third-party licenses are inside the app, at
Orion.app/Contents/Resources/licenses.
TXT

DMG="$OUT/Orion-$LABEL.dmg"
hdiutil create -volname "$NAME $LABEL" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
echo
echo "$DMG"
du -h "$DMG" | cut -f1
