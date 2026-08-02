# Perspective correction — the homography, and why it is not its own node

**Where:** `pipe/Perspective.h` (the matrix and the zoom), `shaders/geometry.slang`
(one line, inside the pass that was already resampling), `pipe/MaskGeometry.h`
(so masks, brush dabs and spots follow the picture), `pipe/DevelopPipeline.cpp`
(composition and staleness).

## Source

**Hartley, R. and Zisserman, A., *Multiple View Geometry in Computer Vision*,
2nd edition, Cambridge University Press, 2004.** ISBN 978-0-521-54051-3.

- §2.3 — a projective transformation of the plane is a linear map on
  homogeneous 3-vectors, a non-singular 3×3 matrix defined up to scale, so
  **eight degrees of freedom**, and it is the most general map that takes
  straight lines to straight lines.
- §4.1 — the **Direct Linear Transformation**. Each point correspondence
  x ↦ x′ contributes two linear equations in the nine entries of H; four
  correspondences in general position give eight, which determines H exactly.
- §4.1.2 — the **inhomogeneous solution**: fix h₃₃ = 1 and solve the resulting
  8×8 system directly, rather than taking the null vector of the 8×9 matrix.
  H&Z note the one case this cannot represent — a true solution with h₃₃ = 0 —
  and it is unreachable here (below).
- §4.4.4 — data normalization: translate the centroid to the origin and scale
  so the mean distance from it is √2, because the DLT is not invariant to the
  coordinate frame it is posed in.

Secondary, for the warp itself rather than the estimation:

- **Heckbert, P. S., *Fundamentals of Texture Mapping and Image Warping*,
  M.Sc. thesis, UC Berkeley, UCB/CSD 89/516, June 1989** — §2.2 gives the same
  four-point map in closed form, §3 is the inverse-mapping argument for why a
  warp is evaluated backwards from the destination.
- **Wolberg, G., *Digital Image Warping*, IEEE Computer Society Press, 1990** —
  §7 on the cost of resampling a resampled image.

⚠️ **Implemented from the descriptions above.** No code was copied from any
implementation, and in particular none from darktable's `ashift` or
RawTherapee's perspective tool, both GPL. The mathematics is a textbook linear
solve; what is written here is written here.

## What the sliders mean

Three controls, all in the same matrix:

| Control | What it does |
|---|---|
| **Vertical** | converging verticals — the building shot looking up |
| **Horizontal** | converging horizontals — the wall shot from one side |
| **Aspect** | the anisotropic squeeze a strong correction leaves behind |

There is deliberately **no fourth Rotate control**. Straighten is already a
rotation about the frame's center, it is already composed into the same
sampling pass, and a second angle would be two controls that do one thing and
disagree about the pivot.

## The four correspondences

Everything is posed in **centered normalized coordinates** of the rotated
frame: the frame is (−1, −1) to (+1, +1), whatever its pixel dimensions.

The map that is solved for is **destination → source**, because the warp is
evaluated backwards (Heckbert §3): every output pixel is written exactly once,
which forward mapping cannot promise.

With vertical travel *k*ᵥ and horizontal travel *k*ₕ, each destination corner
(sₓ, s_y) ∈ {−1, +1}² is asked to come from the source point

    x = sₓ · (1 + kᵥ · s_y)
    y = s_y · (1 + kₕ · sₓ)

Read on the vertical control alone: the destination's **top** row is filled
from a *narrower* strip of the source (±(1 − kᵥ)) and the bottom row from a
wider one (±(1 + kᵥ)). The top is therefore magnified and the bottom shrunk,
which is exactly what pulls a building's converging verticals apart until they
are parallel. Horizontal is the same statement with the axes exchanged.

At kᵥ = kₕ = 0 the four correspondences are the identity, so H is the identity
— and that case is **short-circuited rather than solved**, because Gaussian
elimination promises an accurate answer and not a bit-exact one, and a control
sitting at zero must not move the picture by a rounding error.

**Aspect** is not a correspondence; it is an area-preserving squeeze
premultiplied into the same matrix, diag(1/g, g, 1) with g = 2^(a/2). At a = 0,
g is exactly 1.

### On normalization, H&Z §4.4.4

H&Z's recommendation is to translate the correspondence centroid to the origin
and scale so the mean distance from it is √2. The four points here are the
corners of the centered unit square, so their centroid is already the origin
and their mean distance from it is already √2 — **the coordinates the problem
is posed in are H&Z's normalizing transformation**, exactly, not approximately.
Nothing further is applied, and the reason it is not needed is worth writing
down rather than leaving as a coincidence.

### On the h₃₃ = 0 case, H&Z §4.1.2

The inhomogeneous solve cannot reach a homography whose h₃₃ is zero. h₃₃ = 0
means the destination origin maps to a point at infinity. The travel is clamped
to |k| ≤ 0.35, which keeps the map within a small neighbourhood of the
identity — the solve is additionally checked for a singular pivot and falls
back to the identity, so a matrix that cannot be computed is a control that
does nothing rather than a frame of garbage.

## One resample, not two

The geometry node already composes the camera orientation, the user's quarter
turns, the straighten and the crop into **one** coordinate transform and samples
the source **once**. `geometry.slang` says why in its opening comment: three
nodes would resample three times and soften three times over.

Perspective joins that composition. In the shader it is a single
homogeneous multiply on the coordinate already computed, between the straighten
and the undoing of the quarter turns:

    output pixel → crop → straighten → **H** → quarter turns → bilinear fetch

No new node, no second sampling pass, and the node count of the develop graph is
unchanged. Wolberg §7 is the cost of getting this wrong: bilinear on bilinear is
a triangle filter convolved with itself, and the picture loses high frequencies
it never gets back. `testPerspectiveOneResample` measures it — the same
transform split across two geometry passes comes out measurably softer than the
composed one, on a fixture built to show it.

⚠️ The five host-side pieces — keystone, aspect, auto-scale zoom, and the two
coordinate conversions — are multiplied into **one 3×3 on the host**, once per
geometry change. The shader is handed a matrix and never a chain.

## Auto-scale, and why it needs to know nothing about the crop

A homography pushes part of the destination rectangle outside the source frame;
`sampleBilinear` returns transparent black there, so the corners go empty. Lens
correction has the same problem and the same answer (decision #35,
`pipe/LensGeometry.h`): find the largest zoom that keeps every output pixel
reading real data.

`persp::autoScale` bisects for the largest s ≤ 1 such that the destination
rectangle scaled by s maps entirely inside the frame, and returns **exactly
1.0f** when nothing overreaches — so a neutral control pays no zoom.

Two things make this cheaper and more certain than the lens version:

- **Four corners bound the whole rectangle.** A homography takes lines to
  lines, so the image of the destination rectangle's boundary is the
  quadrilateral through the four mapped corners, and a convex quad lies inside
  a convex rectangle exactly when its vertices do. The lens correction has to
  walk 64 points per edge because its polynomial *bends* the edges; this does
  not.
- **The predicate is an interval, so bisection is exact rather than
  approximate.** The image of the segment {s·p : s ∈ [0,1]} is a straight
  segment from H(0) to H(p); a segment leaving a convex region crosses its
  boundary once. So "fits" is true on [0, s*] and false above it.

  The one way that argument fails is w changing sign inside the rectangle, which
  would send the segment through infinity. w is *affine* in (x, y), so w > 0 at
  the four corners implies w > 0 on their convex hull — and the corners are
  checked.

⚠️ **The scale is computed against the whole frame, not against the crop, and
that is what makes it compose.** The straighten already guarantees, via
`CanvasLayout.constrainedCrop`, that the crop stays inside the turned frame. If
H maps the frame into the frame, then it maps anything already inside the frame
into the frame too. The two guarantees chain without either knowing about the
other, and auto-scale never has to be recomputed when the crop rectangle moves.

## Masks, brush dabs and spots

A mask is placed on the picture the photographer is looking at and *applied* in
`develop:linear`, which runs before the geometry node and sees the whole
untransformed frame. `pipe/MaskGeometry.h` is the transform between the two, and
perspective goes through the same path — or every mask on a corrected photograph
lands somewhere plausible and wrong, which is the failure class this project
fears most.

The **same matrix bytes** the shader is handed are what `mask::toFrame` applies;
there is no second derivation. It converts normalized frame coordinates into the
shader's texel convention (`x·W − 0.5`), applies H, and converts back.

Three properties, each exact or explicitly not:

| Quantity | Under H |
|---|---|
| a mask's **center**, a brush dab, a spot | **exact** — it is a point, and H maps points |
| a linear gradient's **direction** | **exact** — H takes lines to lines, and the image line's direction is the Jacobian applied to the original direction |
| a gradient's **ramp length**, a radial mask's **semi-axes** | **first order** — √\|det J\| at the mask's own center |

The last row is an approximation and is stated rather than implied. A projective
map does not preserve ratios along a line (only cross-ratios), so a linear
gradient's ramp comes out very slightly non-uniform, and an ellipse comes out as
a conic that is not quite the ellipse whose axes we sent. √|det J| is the same
compromise `mask::lengthToFrame` already makes for the crop — the geometric mean
of the two axis scales — and it is exact wherever the map is locally isotropic,
which includes the whole frame at k = 0.

**And the cost of that is a measured number, not a hedge.** `maskcheck` compares
the render against the *overlay's* own transcription of the kernel and demands
that every cell drawn clear come back bit-identical. On `_PIC8220`, a hard-edged
radial mask (feather 0.06):

| Vertical | Mask width, as a fraction of the frame | Clear cells that leak |
|---|---|---|
| 0.45 | 0.10, 0.20, **0.28** | none |
| 0.45 | 0.34 | 2 of 60, worst 0.0105 luma at −2 EV |
| 1.00 | 0.34 | 2 of 60, worst 0.0617 luma |
| 0.20 | 0.34 | none |

Exact up to a mask about a quarter of the frame across at a moderate
correction, degrading at the **rim** and never at the centre beyond that.
`repro/perspective-carries-the-mask.txt` sits at 0.28 — the edge of the exact
range — so it cannot quietly get worse. The fix, and why it was not taken in the
same session, is [`UNSOURCED.md`](UNSOURCED.md) §24 and `ROADMAP.md`.

## The range is a control choice, not a measurement

Corner travel at full slider is **0.35**, and aspect's g spans 2^±½. Neither
comes from a source; both are the same kind of decision as the lens sliders'
|k₁| ≤ 0.35, which is a range and not a constant of nature. They are in
[`UNSOURCED.md`](UNSOURCED.md) §24 rather than dressed up as derived. What the
travel actually costs is auto-scale: more correction is more zoom is more
magnification of the source, so the ceiling is a picture-quality judgement.

## What is pinned

`orion-tests`:

- `persp::keystone` at zero returns the **exact** identity, and `autoScale` on
  it returns **exactly** 1.0f
- the GPU's sampled source coordinate agrees with `persp::apply` on the host,
  pixel by pixel, under a keystone that is not symmetric in either axis — the
  check that catches a transposed matrix or a dropped perspective divide
- a synthetic frame whose vertical lines converge comes out with them
  **parallel**, measured as the top-row and bottom-row spacing agreeing
- the whole transform in one geometry pass keeps **more detail** than the same
  transform split across two
- a strong keystone with auto-scale on leaves **no transparent pixel** anywhere
  in the output
- `mask::toFrame` and `mask::fromFrame` round-trip under a keystone, and a mask
  center follows the picture the geometry node produced

`repro/`: `perspective-carries-the-mask.txt` — the control moves the picture, a
mask stays on its subject with the control up, and both survive a reopen.

`orion-bench`: a `perspective` control probe with a magnitude floor, per
decision #37.
