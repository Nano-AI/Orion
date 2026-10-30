#!/bin/zsh
# Links native/dawn_surface.mm against the source build of Dawn into bin-native/libdawn_surface.dylib, which the C# app loads.
set -euo pipefail
here=${0:A:h}
spike=$here/..
dawn=$spike/../../third_party/dawn-build   # tools/build-dawn.sh: pinned source build, macOS 14+ (#361)
[[ -f $dawn/lib/libwebgpu_dawn.a ]] || { echo "no Dawn at $dawn: run tools/build-dawn.sh (through the heavy-job guard)" >&2; exit 1; }

mkdir -p "$spike/bin-native"
clang++ -std=c++20 -mmacosx-version-min=14.0 -fobjc-arc -dynamiclib -O2 -I"$dawn/include" \
    "$here/dawn_surface.mm" "$dawn/lib/libwebgpu_dawn.a" \
    -framework Foundation -framework IOSurface -framework QuartzCore -framework Cocoa \
    -framework IOKit -framework Metal -framework CoreFoundation \
    -install_name @rpath/libdawn_surface.dylib -o "$spike/bin-native/libdawn_surface.dylib"
# The headless check: no window. Run bin-native/dawn_selftest.
clang++ -std=c++20 -mmacosx-version-min=14.0 -fobjc-arc -O2 "$here/selftest.mm" -L"$spike/bin-native" -ldawn_surface \
    -Wl,-rpath,@executable_path -framework Foundation -framework IOSurface -framework Metal \
    -o "$spike/bin-native/dawn_selftest"
echo "$spike/bin-native/libdawn_surface.dylib $spike/bin-native/dawn_selftest"
