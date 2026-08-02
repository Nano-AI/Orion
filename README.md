# Orion

A fast, subscription-free raw photo editor for macOS.

Adobe's grip on raw editing makes serious photo work expensive for students and
hobbyists. The open alternatives are capable but dated or hard to learn. Orion
aims at the gap: a GPU pipeline quick enough that adjustments feel instant, a
toolset capable of transforming an image rather than nudging it, and an
interface that stays out of the way of the photograph.

**Status: M0–M2 complete, M3 not started.** It browses a folder, culls, develops,
crops, and exports with metadata and a color profile. The panel covers white
balance, tone, a tone curve, three-way color grading, an eight-band color
mixer, profiled noise reduction, lens corrections, sharpening, highlight
recovery, crop and straighten. Undo is unlimited and edits persist per photo in XMP sidecars.

**Not there yet:** masking, healing, a lens database, tethering. See [`planning/ROADMAP.md`](planning/ROADMAP.md) and
[`planning/STATUS.md`](planning/STATUS.md), which is the honest list.

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
decode → linearize + white balance + white clip → RCD demosaic
       → highlight reconstruction → wavelet denoise (4 scales)
       → lens corrections → sharpen → camera matrix
       → guided filter → tone, color → three-way grade
       → AgX display transform + curve → crop, straighten, orientation
```

27 nodes, about 4 GiB of intermediates on a 24 MP frame.

Everything between the camera matrix and the display transform is scene-linear
and unbounded. There is exactly one display transform, and it is at the end.

The **white clip** in the first node is small and load-bearing. A sensor
saturates at one count for every channel, so a blown highlight arrives as
(S, S, S); white balance then multiplies each channel by its own gain and what
was a white light is, in ratio, the gains themselves. Nothing downstream can
undo that, because every stage after it preserves ratios. Without the clip,
every clipped light in a night frame renders magenta.

## Algorithms are cited, not invented

Every non-trivial filter cites a published reference in [`research/`](research/),
and anything that does not is listed honestly in
[`research/UNSOURCED.md`](research/UNSOURCED.md).

This rule exists because plausible-looking constants once shipped a purple cast
on every image — a class of bug that is invisible to inspection and obvious to
arithmetic. Citing the source makes the numbers checkable; testing the invariant
keeps them right.

Notable ports: RCD demosaic, the fast guided filter (He & Sun, arXiv:1505.00996)
for local highlight and shadow recovery, AgX (Sobotka) as the display transform,
monotone cubic Hermite (Fritsch & Carlson, 1980) for tone curves, à-trous
wavelet denoising (Starck et al., 2007) with a per-frame Poisson–Gaussian noise
fit (Foi et al., 2008), cross-channel highlight reconstruction (Masood, Zhu &
Tappen, CGF 2009), ASC CDL v1.2 for the grading wheels, and lensfun's lens
correction models.

No GPL code is copied. Implementing a *published algorithm* from its description
is fine — mathematics is not copyrightable — and each entry says which it is.

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

208 engine checks, 2067 viewport checks.

The GPU tests matter most. Pure maths tests pass happily on code that renders
garbage, because they never touch a texture — two shipped bugs proved it.

### The screenshot harness

```sh
./build/Orion.app/Contents/MacOS/Orion --screenshot out.png --photo x.ARW \
    --scene light [--measure x,y,w,h] [--size 1680x1050]
```

Renders the real view hierarchy offscreen, so no Screen Recording permission is
needed. `--measure` prints per-channel mean, standard deviation, saturation and
luma over a region of the engine's output — which is how "there is purple in my
photo" became a number that could be watched going down.

**What it cannot see:** the Metal canvas (AppKit cannot capture a Metal layer,
so it draws a still), and any 3D transform (`cacheDisplay` skips them). Both
limits are recorded in `planning/STATUS.md` rather than left to be rediscovered.
