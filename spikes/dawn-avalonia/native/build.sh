#!/bin/zsh
# Fetches Dawn's prebuilt (pinned, hash-checked) and links native/dawn_surface.mm
# against it into bin-native/libdawn_surface.dylib, which the C# app loads.
set -euo pipefail
here=${0:A:h}
spike=$here/..
tag=v20261027.184652
name=Dawn-106704a9ac3fc21b1580ea85aea302ac17d416f1-macos-latest-Release
sha=d4f419dcbe81de4b86e6fbf172b647f41c67820a925efca55dae3bd43248d8e2
dawn=$spike/third_party/dawn

if [[ ! -f $dawn/lib/libwebgpu_dawn.a ]]; then
    mkdir -p "$spike/third_party"
    curl -fL -o "$spike/third_party/$name.tar.gz" "https://github.com/google/dawn/releases/download/$tag/$name.tar.gz"
    echo "$sha  $spike/third_party/$name.tar.gz" | shasum -a 256 -c -
    tar xzf "$spike/third_party/$name.tar.gz" -C "$spike/third_party"
    mv "$spike/third_party/$name" "$dawn"
    rm "$spike/third_party/$name.tar.gz"
fi

mkdir -p "$spike/bin-native"
clang++ -std=c++20 -fobjc-arc -dynamiclib -O2 -I"$dawn/include" \
    "$here/dawn_surface.mm" "$dawn/lib/libwebgpu_dawn.a" \
    -framework Foundation -framework IOSurface -framework QuartzCore -framework Cocoa \
    -framework IOKit -framework Metal -framework CoreFoundation \
    -install_name @rpath/libdawn_surface.dylib -o "$spike/bin-native/libdawn_surface.dylib"
# The headless check: no window. Run bin-native/dawn_selftest.
clang++ -std=c++20 -fobjc-arc -O2 "$here/selftest.mm" -L"$spike/bin-native" -ldawn_surface \
    -Wl,-rpath,@executable_path -framework Foundation -framework IOSurface -framework Metal \
    -o "$spike/bin-native/dawn_selftest"
echo "$spike/bin-native/libdawn_surface.dylib $spike/bin-native/dawn_selftest"
