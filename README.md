# Orion

A fast, subscription-free raw photo editor for macOS.

Adobe's grip on raw editing makes serious photo work expensive for students and
hobbyists. The open alternatives are capable but dated or hard to learn. Orion
aims at the gap: a GPU pipeline quick enough that adjustments feel instant, a
toolset capable of transforming an image rather than nudging it, and an
interface that stays out of the way of the photograph.

**Status: early.** It opens a raw file, develops it, and exports. There is no
library, no crop, and no highlight recovery yet. See
[`planning/ROADMAP.md`](planning/ROADMAP.md) for what is done and what is not.

---

## How it is built

A C++20 engine drives a Metal compute graph; a SwiftUI shell displays its output
texture directly, with no readback. Shaders are authored in Slang so a Vulkan or
D3D backend stays reachable.

Edits form a **DAG, not a chain**, so moving a slider recomputes only what sits
downstream of it. On an M4, a 24 MP Sony ARW re-renders an exposure change in
about 8 ms against a 16 ms budget — at full resolution, with no preview proxy.
That is why zooming to 100% shows real pixels: the full-resolution result is
already in a texture.

```
decode → white balance → demosaic → camera matrix
       → guided filter → tone, color, curve
       → AgX display transform → orientation
```

Everything between the camera matrix and the display transform is scene-linear
and unbounded. There is exactly one display transform, and it is at the end.

## Algorithms are cited, not invented

Every non-trivial filter cites a published reference in [`research/`](research/),
and anything that does not is listed honestly in
[`research/UNSOURCED.md`](research/UNSOURCED.md).

This rule exists because plausible-looking constants once shipped a purple cast
on every image — a class of bug that is invisible to inspection and obvious to
arithmetic. Citing the source makes the numbers checkable; testing the invariant
keeps them right.

Notable ports: the guided filter (He, Sun & Tang, ECCV 2010) for local highlight
and shadow recovery, AgX (Sobotka) as the display transform, monotone cubic
Hermite (Fritsch & Carlson, 1980) for tone curves.

## Building

Requires macOS 14+, Xcode with the Metal toolchain, and Homebrew.

```sh
brew install cmake ninja libraw little-cms2
# Slang: extract a macOS release from github.com/shader-slang/slang
# into third_party/slang/

cmake -S . -B build -G Ninja
cmake --build build

open build/Orion.app
```

## Tests

```sh
./build/apps/tests/orion-tests      # engine maths, plus real GPU renders
./build/orion-viewport-tests        # canvas geometry
./build/apps/bench/orion-bench file.ARW   # latency gate and per-control checks
```

The GPU tests matter most. Pure maths tests pass happily on code that renders
garbage, because they never touch a texture — two shipped bugs proved it.
