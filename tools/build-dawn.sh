#!/usr/bin/env bash
# Builds Dawn (WebGPU) from source at a pinned commit into third_party/dawn-build/
# (git-ignored): include/, lib/ (libwebgpu_dawn.a, or webgpu_dawn.lib on Windows),
# lib/cmake/. One backend per system (#361, #376):
#   macOS    Metal, macOS 14+. Replaces the spike's GitHub nightly, which is minos
#            26.0 and has no notices (#354, #361).
#   Windows  D3D12 (and WARP, its software adapter, on a machine with no GPU).
#            Run from a shell with MSVC's environment (vcvars), clang-cl on PATH.
#   Linux    Vulkan (lavapipe is the software driver CI uses).
#
# Run it through the heavy-job guard; on this Mac about 3.5 min and 1.4 GB peak from nothing.
#   tools/build-dawn.sh            # this machine's architecture
#   tools/build-dawn.sh fetch      # fetch the source only
#   ARCHS="arm64;x86_64" tools/build-dawn.sh   # macOS universal, if Intel stays supported (untested)
#
# Fetches with git and Dawn's own fetch_dawn_dependencies.py (shallow, no depot_tools).
# The source tree stays in third_party/dawn-src/ (licence texts, rebuilds); the object
# tree is deleted after install.
set -euo pipefail
for t in cmake ninja git; do command -v $t >/dev/null || { echo "need $t" >&2; exit 1; }; done
py=$(command -v python3 || command -v python) || { echo "need python3" >&2; exit 1; }
root=$(cd "$(dirname "$0")/.." && pwd)
commit=106704a9ac3fc21b1580ea85aea302ac17d416f1   # same commit the spike validated (#354)
src=$root/third_party/dawn-src
obj=$root/third_party/dawn-obj
out=$root/third_party/dawn-build

if [[ ! -f $src/.fetched-$commit ]]; then
    rm -rf "$src"; mkdir -p "$src"
    git -C "$src" init -q
    git -C "$src" fetch -q --depth 1 https://dawn.googlesource.com/dawn "$commit"
    git -C "$src" checkout -q FETCH_HEAD
    "$py" "$src/tools/fetch_dawn_dependencies.py" --directory "$src" --shallow
    touch "$src/.fetched-$commit"
fi
[[ ${1:-} == fetch ]] && exit 0   # tools/build-dawn.sh fetch: source only

# Everything off except Tint's WGSL reader/writer and the one backend this system
# uses, with the shader writer that backend needs.
off=(-DDAWN_ENABLE_METAL=OFF -DDAWN_ENABLE_VULKAN=OFF -DDAWN_ENABLE_D3D11=OFF -DDAWN_ENABLE_D3D12=OFF
     -DTINT_BUILD_MSL_WRITER=OFF -DTINT_BUILD_SPV_WRITER=OFF -DTINT_BUILD_HLSL_WRITER=OFF)
case "$(uname -s)" in
    Darwin)
        # Tint's asserts embed __FILE__; without the map every binary carries /Users/<name>/,
        # which package-app.sh's personal-data gate rejects.
        map="-ffile-prefix-map=$src/=dawn/ -ffile-prefix-map=$obj/=dawn-obj/"
        system=("${off[@]}" -DDAWN_ENABLE_METAL=ON -DTINT_BUILD_MSL_WRITER=ON
                -DCMAKE_C_FLAGS="$map" -DCMAKE_CXX_FLAGS="$map"
                -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DCMAKE_OSX_ARCHITECTURES="${ARCHS:-arm64}") ;;
    MINGW*|MSYS*|CYGWIN*)
        # A CI path (D:\a\...) is no one's home directory; clang-cl, as the engine is built.
        system=("${off[@]}" -DDAWN_ENABLE_D3D12=ON -DTINT_BUILD_HLSL_WRITER=ON
                -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl) ;;
    Linux)
        map="-ffile-prefix-map=$src/=dawn/ -ffile-prefix-map=$obj/=dawn-obj/"
        # Position-independent: the engine, and Dawn in it, link into the shell's .so.
        system=("${off[@]}" -DDAWN_ENABLE_VULKAN=ON -DTINT_BUILD_SPV_WRITER=ON
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
                -DCMAKE_C_FLAGS="$map" -DCMAKE_CXX_FLAGS="$map") ;;
    *) echo "build-dawn.sh: no backend for $(uname -s)" >&2; exit 1 ;;
esac

cmake -S "$src" -B "$obj" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$out" \
    -DDAWN_FETCH_DEPENDENCIES=OFF \
    -DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC \
    -DDAWN_ENABLE_INSTALL=ON \
    "${system[@]}" \
    -DDAWN_ENABLE_OPENGLES=OFF -DDAWN_ENABLE_DESKTOP_GL=OFF \
    -DDAWN_ENABLE_NULL=OFF -DDAWN_USE_GLFW=OFF -DDAWN_USE_X11=OFF -DDAWN_USE_WAYLAND=OFF \
    -DDAWN_ENABLE_SWIFTSHADER=OFF \
    -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF -DDAWN_BUILD_BENCHMARKS=OFF \
    -DDAWN_BUILD_PROTOBUF=OFF -DDAWN_BUILD_NODE_BINDINGS=OFF \
    -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_BENCHMARKS=OFF -DTINT_BUILD_CMD_TOOLS=OFF \
    -DTINT_BUILD_FUZZERS=OFF -DTINT_BUILD_IR_BINARY=OFF -DDAWN_ENABLE_SPIRV_VALIDATION=OFF \
    -DTINT_BUILD_SPV_READER=OFF \
    -DTINT_BUILD_GLSL_WRITER=OFF -DTINT_BUILD_GLSL_VALIDATOR=OFF \
    -DTINT_BUILD_WGSL_READER=ON -DTINT_BUILD_WGSL_WRITER=ON
cmake --build "$obj"
# Install beside the old output and swap, so a failed install leaves the previous build.
rm -rf "$out.new"
cmake --install "$obj" --prefix "$out.new"
rm -rf "$out"; mv "$out.new" "$out"
rm -rf "$obj"
echo "dawn: $out ($(du -sh "$out" | cut -f1)), commit $commit"
