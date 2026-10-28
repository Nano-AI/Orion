#!/bin/zsh
# Links the whole engine into one dylib C# can load: shim_* for adjustments by
# name, and every orion_* facade function, exported by -force_load so a slice
# can call them directly. Needs `cmake --build build --target orion_engine`.
set -euo pipefail
here=${0:A:h}
root=$here/../../..
out=$here/../bin-native
mkdir -p "$out"
# This tree's own build first (a stream that changes engine/ must link it), else the main checkout's.
engine=$root/build/engine/liborion_engine.a
[[ -f $engine ]] || engine=$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/../build/engine/liborion_engine.a
[[ -f $engine ]] || { echo "build.sh: no liborion_engine.a — run cmake --build build --target orion_engine in the main checkout" >&2; exit 1; }
python3 "$here/gen_fields.py" "$root/engine/include/orion/orion.h" "$out/fields.inc" "$out/mask_fields.inc"
clang++ -std=c++20 -fobjc-arc -dynamiclib -O2 \
    -I"$root/engine/include" -I"$out" \
    "$here/shim.mm" -Wl,-force_load,"$engine" \
    /opt/homebrew/opt/libraw/lib/libraw_r.dylib \
    /opt/homebrew/opt/opencv/lib/libopencv_{core,imgproc,features,geometry,video}.dylib \
    -framework Metal -framework Foundation -framework CoreGraphics -framework ImageIO \
    -o "$out/liborion_shim.dylib"
echo "$out/liborion_shim.dylib (engine: ${engine:A})"
