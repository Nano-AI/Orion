#!/bin/zsh
# Links the whole engine into one dylib C# can load: shim_* for adjustments by
# name, and every orion_* facade function, exported by -force_load so a slice
# can call them directly. Needs `cmake --build build --target orion_engine`.
set -euo pipefail
here=${0:A:h}
root=$here/../..
out=$here/../bin-native
mkdir -p "$out"
# This tree's own build first (a stream that changes engine/ must link it), else the main checkout's.
engine=${ORION_ENGINE_LIB:-$root/build/engine/liborion_engine.a}   # override: another build's engine
[[ -f $engine ]] || engine=$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/../build/engine/liborion_engine.a
[[ -f $engine ]] || { echo "build.sh: no liborion_engine.a — run cmake --build build --target orion_engine in the main checkout" >&2; exit 1; }
# The engine's Dawn backend (#375), only when this engine archive was built
# with it (-DORION_DAWN=OFF leaves DawnDevice out, and needs no Dawn at all).
dawn=()
if ar t "$engine" | grep -q '^DawnDevice\.mm\.o$'; then
    dawn=("$root/third_party/dawn-build/lib/libwebgpu_dawn.a" -framework QuartzCore -framework IOKit)
fi
python3 "$here/gen_fields.py" "$root/engine/include/orion/orion.h" "$out/fields.inc" "$out/mask_fields.inc"
clang++ -std=c++20 -fobjc-arc -dynamiclib -O2 \
    -I"$root/engine/include" -I"$out" \
    "$here/shim.mm" "$here/present.mm" "$here/matte.mm" -Wl,-force_load,"$engine" \
    /opt/homebrew/opt/libraw/lib/libraw_r.dylib \
    /opt/homebrew/opt/opencv/lib/libopencv_{core,imgproc,features,geometry,video}.dylib \
    /opt/homebrew/opt/jpeg-xl/lib/libjxl{,_threads}.dylib "${dawn[@]}" \
    -framework AppKit -framework Metal -framework Foundation -framework CoreGraphics -framework ImageIO \
    -framework IOSurface -framework Vision -framework CoreVideo -install_name @rpath/liborion_shim.dylib -o "$out/liborion_shim.dylib"
# The shared surface's headless check (present.mm): bin-native/present_test RAW. No window.
clang++ -std=c++20 -fobjc-arc -O2 "$here/present_test.mm" -L"$out" -lorion_shim -Wl,-rpath,@executable_path \
    -framework Foundation -framework IOSurface -framework Metal -o "$out/present_test"
echo "$out/liborion_shim.dylib $out/present_test (engine: ${engine:A})"
