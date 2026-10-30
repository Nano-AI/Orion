#!/bin/zsh
# Builds Dawn (WebGPU) from source at a pinned commit, Metal backend only, for
# macOS 14+, into third_party/dawn-build/ (git-ignored): include/, lib/libwebgpu_dawn.a,
# lib/cmake/. Replaces the spike's GitHub nightly, which is minos 26.0 and has no notices (#354, #361).
#
# Run it through the heavy-job guard; about 3.5 min and 1.4 GB peak from nothing.
#   tools/build-dawn.sh            # arm64 (this Mac)
#   tools/build-dawn.sh fetch      # fetch the source only
#   ARCHS="arm64;x86_64" tools/build-dawn.sh   # universal, if Intel stays supported (untested)
#
# Fetches with git and Dawn's own fetch_dawn_dependencies.py (shallow, no depot_tools).
# The source tree stays in third_party/dawn-src/ (licence texts, rebuilds); the object
# tree is deleted after install.
set -euo pipefail
for t in cmake ninja python3 git; do command -v $t >/dev/null || { echo "need $t" >&2; exit 1; }; done
root=${0:A:h:h}
commit=106704a9ac3fc21b1580ea85aea302ac17d416f1   # same commit the spike validated (#354)
src=$root/third_party/dawn-src
obj=$root/third_party/dawn-obj
out=$root/third_party/dawn-build

if [[ ! -f $src/.fetched-$commit ]]; then
    rm -rf "$src"; mkdir -p "$src"
    git -C "$src" init -q
    git -C "$src" fetch -q --depth 1 https://dawn.googlesource.com/dawn "$commit"
    git -C "$src" checkout -q FETCH_HEAD
    python3 "$src/tools/fetch_dawn_dependencies.py" --directory "$src" --shallow
    touch "$src/.fetched-$commit"
fi
[[ ${1:-} == fetch ]] && exit 0   # tools/build-dawn.sh fetch: source only

# Tint's asserts embed __FILE__; without the map every binary carries /Users/<name>/,
# which package-app.sh's personal-data gate rejects.
map="-ffile-prefix-map=$src/=dawn/ -ffile-prefix-map=$obj/=dawn-obj/"
cmake -S "$src" -B "$obj" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$map" -DCMAKE_CXX_FLAGS="$map" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DCMAKE_OSX_ARCHITECTURES="${ARCHS:-arm64}" \
    -DCMAKE_INSTALL_PREFIX="$out" \
    -DDAWN_FETCH_DEPENDENCIES=OFF \
    -DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC \
    -DDAWN_ENABLE_INSTALL=ON \
    -DDAWN_ENABLE_METAL=ON \
    -DDAWN_ENABLE_VULKAN=OFF -DDAWN_ENABLE_OPENGLES=OFF -DDAWN_ENABLE_DESKTOP_GL=OFF \
    -DDAWN_ENABLE_NULL=OFF -DDAWN_USE_GLFW=OFF -DDAWN_USE_X11=OFF -DDAWN_USE_WAYLAND=OFF \
    -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF -DDAWN_BUILD_BENCHMARKS=OFF \
    -DDAWN_BUILD_PROTOBUF=OFF -DDAWN_BUILD_NODE_BINDINGS=OFF \
    -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_BENCHMARKS=OFF -DTINT_BUILD_CMD_TOOLS=OFF \
    -DTINT_BUILD_FUZZERS=OFF -DTINT_BUILD_IR_BINARY=OFF -DDAWN_ENABLE_SPIRV_VALIDATION=OFF \
    -DTINT_BUILD_SPV_READER=OFF -DTINT_BUILD_SPV_WRITER=OFF \
    -DTINT_BUILD_GLSL_WRITER=OFF -DTINT_BUILD_GLSL_VALIDATOR=OFF -DTINT_BUILD_HLSL_WRITER=OFF \
    -DTINT_BUILD_MSL_WRITER=ON -DTINT_BUILD_WGSL_READER=ON -DTINT_BUILD_WGSL_WRITER=ON
cmake --build "$obj"
# Install beside the old output and swap, so a failed install leaves the previous build.
rm -rf "$out.new"
cmake --install "$obj" --prefix "$out.new"
rm -rf "$out"; mv "$out.new" "$out"
rm -rf "$obj"
echo "dawn: $out ($(du -sh "$out" | cut -f1)), commit $commit"
