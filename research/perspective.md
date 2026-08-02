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

Four properties, each exact or explicitly not:

| Quantity | Under H |
|---|---|
| a mask's **center**, a brush dab, a spot | **exact** — it is a point, and H maps points |
| a linear gradient's **direction** | **exact** — H takes lines to lines, and the image line's direction is the Jacobian applied to the original direction |
| a radial mask's **semi-axes** and **angle** | **exact under the derivative** — the ellipse J makes of the ellipse, in closed form. All of the anisotropy; none of the curvature |
| a gradient's **ramp length** | **exact under the derivative** — \|J·u\| along the ramp's own pre-image direction. All of the anisotropy; none of the curvature, and none of the non-uniformity along the ramp |

⚠ The fourth row read **isotropic, first order — √\|det J\| at the mask's own
center** until 2026-08-02; see *The ramp*, below, for what replaced it and for
how little it was worth.

### The ellipse, and what it did and did not fix

The third row shipped with decision #100 as √|det J| — **one isotropic number
standing in for a general 2×2** — and was replaced on 2026-08-01. The
replacement is thirty lines in `mask::radiusToFrame`:

- An ellipse with semi-axes (aₓ, a_y) at angle φ is the image of the unit disc
  under **A = R(φ)·diag(aₓ, a_y)**; its image under J is the image of the unit
  disc under **B = J·A**.
- B's semi-axis lengths are its singular values, along its left singular
  vectors — **Golub & Van Loan, *Matrix Computations*, 4th ed., JHU Press 2013,
  ISBN 978-1-4214-0794-4, §2.4.1**: the image of the unit sphere under a matrix
  is a hyperellipse with semiaxes σᵢuᵢ.
- Both are read off **S = B·Bᵀ**, symmetric positive semi-definite, whose 2×2
  eigenproblem is closed form — **§8.5.2**, the symmetric Schur decomposition
  Jacobi's method is built on. λ± = (p+r)/2 ± √(((p−r)/2)² + q²), ψ = ½·atan2(2q, p−r).

⚠ **Smith's closed form (CACM 1961) is in this repository and was not reused.**
`SkyDetector.Stats.largestVariance()` is Swift, is the 3×3 case, and returns the
largest eigenvalue and no eigenvector; this needs both roots and an axis, in
C++, on the engine side of the facade. Smith's construction *reduces* to the
quadratic above at n = 2, so what is written is that reduction and not a second
derivation.

⚠ **This is the image under the map's derivative, which is not the image under
the map.** What it removes is the whole first-order error — all of the
anisotropy. What remains is second order: the map's curvature across the mask,
which no Jacobian at a point can see. The version it replaced was also called
exact by somebody who had checked the middle of the frame.

**Both halves are measured.** `maskcheck` compares the render against the
*overlay's* own transcription of the kernel and demands that every cell drawn
clear come back bit-identical. On `_PIC8220`, hard-edged radial masks (feather
0.06), `maskcheck 10 -2.0`, measured 2026-08-01 — isotropic → ellipse:

**Aspect, which is diag(1/g, g): exactly linear, so zero curvature.** This is
where the anisotropy is the whole error.

| Aspect | Mask | Isotropic | Ellipse |
|---|---|---|---|
| +1.0 | 0.20 round, centred | 4 of 84, worst **0.1461** | **none** |
| −1.0 | 0.20 round, centred | 4 of 84, worst 0.1207 | **none** |
| +0.5 | 0.20 round, centred | 4 of 84, worst 0.0567 | **none** |
| +1.0 | 0.24 × 0.14 at 0.6 rad | 2 of 80, worst 0.0067 | **none** |

√|det J| for a squeeze is exactly 1, so the old code left a mask **round while
the picture under it was stretched two to one**. Not an approximation going soft
at the rim; the shape dropped entirely.

**Vertical keystone, where the curvature dominates at large sizes**, mask
centred (0.40, 0.45):

| Vertical | Mask | Isotropic | Ellipse |
|---|---|---|---|
| 0.45 | 0.34 × 0.22 | none | **none** |
| 1.00 | 0.34 × 0.22 | none | **none** |
| 1.00 | 0.34 × 0.28 | 1 of 56, 0.0044 | 1 of 56, 0.0041 |
| 0.45 | 0.34 × 0.34 | 2 of 48, 0.0214 | 2 of 48, **0.0163** |
| 1.00 | 0.34 × 0.34 | 4 of 48, 0.1001 | 4 of 48, **0.0862** |
| 0.45 | 0.30 × 0.34 | 2 of 58, 0.0198 | 2 of 58, **0.0145** |
| 1.00 | 0.30 × 0.34 | 4 of 58, 0.0989 | 4 of 58, **0.0836** |
| 0.20 | any of the above | none | **none** |

So the ellipse buys about a fifth of a keystone's rim error and all of a
squeeze's, which is exactly what "removes the first order, leaves the second"
predicts. A mask larger than about a third of the frame under a strong keystone
still disagrees with the overlay at its rim, and that is now a statement about
the map's curvature rather than about a shortcut.

⚠ **The table printed here before 2026-08-01 was not reproducible.** It recorded
`0.34 × 0.22` leaking 2 of 60 cells at 0.0105 luma under vertical 0.45; that
configuration measures **64 clear cells and no leak at all**, on the build that
fixed this *and on the one before it*, run through this very scenario file. The
recorded cell count does not match either. The leak is
driven by the mask's extent along the axis the keystone *stretches*, and the old
sweep varied the other one. The numbers above are the ones a re-run produces.

`repro/perspective-carries-the-mask.txt` sits at 0.34 now, and its aspect
section is what fails when the ellipse is reverted.

#### How large the curvature actually is

⚠ **"About a fifth of a keystone's rim error" undersells it, and the number that
matters is not a fifth of anything.** Measured directly rather than inferred from
cell counts: the shipping ellipse against the exact answer — carry each frame
point out to the displayed picture and evaluate the mask the photographer drew —
over a 600 × 600 grid, feather 0.06, roundness 2:

| Correction | Mask | max ΔCoverage | mean Δ | frame differing by >10⁻³ |
|---|---|---|---|---|
| aspect +1.0 | 0.20 round | **0.0000** | 0.00000 | 0.00% |
| vertical 0.20 | 0.34 × 0.34 | 0.9448 | 0.01609 | 4.89% |
| vertical 0.45 | 0.34 × 0.22 | 0.9996 | 0.01178 | 2.95% |
| vertical 0.45 | 0.34 × 0.34 | 1.0000 | 0.02832 | 5.59% |
| vertical 1.00 | 0.34 × 0.28 | 1.0000 | 0.02615 | 4.16% |
| vertical 1.00 | 0.34 × 0.34 | **1.0000** | 0.03913 | 5.81% |
| vertical 1.00 + horizontal 0.60 | 0.34 × 0.34 | 1.0000 | 0.02525 | 3.74% |
| vertical 1.00 | 0.10 × 0.10 | 0.9947 | 0.00095 | 0.25% |

**The first row is the harness checking itself.** An aspect squeeze is exactly
linear, so it has no curvature, so an exact derivative must be exactly right —
and the measurement returns 0.0000, not 10⁻⁶. That is what makes the rest of the
column believable.

The rim reaches **full coverage difference**: at a keystone of 1.00 there are
pixels the render covers completely and the interface draws clear. It reads as
"a fifth off" through `maskcheck` because that instrument counts *cells*, and a
cell is 1% of the frame at 10 × 10 — most of the disagreement is inside cells the
overlay already classifies as covered or falloff, where it is not counted.

The last row is the useful consolation: a mask a tenth of the frame wide is
wrong over **0.25%** of the picture. The error is quadratic in the mask's size,
as second order requires.

### The ramp, which the ellipse did not touch

The ellipse rescued the *radial* mask's extent and left the linear gradient's
alone: the fourth row stayed √|det J| for another day. A ramp has exactly one
direction, and the geometric mean of two axis scales is not the scale along it.
`mask::lengthAlong` (decision #134, 2026-08-02) returns **|J·u|** for the ramp's
own pre-image direction — the length of the image of a unit vector, which is the
same singular-value picture as above read along one direction instead of two,
and exactly 1 at the identity by construction rather than by rounding.

The direction was never the problem. H takes lines to lines, so the ramp's
*angle* was already exact under the second row of the table; only its length went
through the isotropic number.

⚠ **The size of the error is the interesting part, and it is small.** Measured on
`_PIC8220`, off-centre angled ramp at 0.6 rad, length 0.20, feather 0.02, over a
6 × 6 grid of patches across the whole frame, current build against the same
build with `lengthAlong` reverted to `placed.scale`:

| Correction | Patches that move | Worst |
|---|---|---|
| aspect squeeze +1.0 | 7 of 36 | **0.0003** luma |
| keystone, V 0.45 + H 0.30 | 10 of 36 | **0.0018** luma |

The keystone row corroborates the 0.6753 → 0.6734 recorded against decision
#134. The aspect row **corrects it**.

⚠ **The first version of this section said the aspect squeeze changes "nothing at
all", because `perspectiveAspect` leaves `Placement::jac` the identity. Both
halves of that are false.** Printed directly, `Placement::jac` under
`perspectiveAspect +1.0` is **diag(0.500050, 1.000100)** — constant across the
frame, because a squeeze is exactly linear, but nowhere near 1 — and the frames
are not byte-identical either. The claim was written to explain a real
observation (a ramp fixture built around the squeeze passed with the fix
reverted) and it explained it wrongly. The observation survives; its reason was
invented. It had reached five documents before it was checked.

The honest statement is duller: the term is worth **0.0003 to 0.0018 luma**
wherever it is measured, against the **0.1461** the radial first-order error was
worth, and that is why the call site is not pinned.

⚠ **So the call site is not pinned, and that is deliberate rather than
overlooked.** `lengthAlong` itself has four checks in `tests_mask_geom.cpp` —
identity, `diag(2, ½)` along each axis where √|det J| would answer 1, and a shear
along y where the answer is √1.25 and cannot come from reading one matrix entry.
What no check covers is `DevelopMask.cpp` calling it: 0.002 luma flips no cell at
12 cells or at 20, and a golden-value check with that margin fails for reasons
other than the defect. `repro/perspective-carries-the-mask.txt` §4b carries the
same warning in the file that would otherwise imply the coverage.

⚠ **The first fixture written for this could not fail**, and it was built around
the aspect squeeze — the case that had rescued the radial mask. It passed with
the fix reverted. The reason recorded at the time was wrong, and the true reason
is above: the term is small everywhere, not absent there.

### And the level sets, which are wrong today

⚠ **Chasing the paragraph above turned up a first-order defect in the shipping
build, in the same mask kind.** A gradient's *direction* is exact and its
*length* now goes through |J·u| — and its **level sets are still wrong**, under
any correction that is not conformal.

`mask_component.slang` kind 1 parameterizes the ramp by two endpoints and
projects onto the segment between them:

    t = dot(q − zero, d) / dot(d, d),    d = full − zero

so its level sets are the lines **perpendicular to d in frame coordinates**. The
mask the photographer drew has level sets perpendicular to the ramp in *display*
coordinates, and a linear map does not preserve perpendicularity. t is a linear
**functional**, not a vector: it transforms by the inverse transpose, J⁻ᵀ, while
the endpoints transform by J. For a conformal J — a rotation and a uniform scale
— the two agree up to a factor, which is every case anybody checks by hand.

Measured against the exact answer, worst coverage difference anywhere in the
frame, ramp at (0.30, 0.25), 0.6 rad, length 0.20:

| Correction | Shipping (endpoints) | Carried by J⁻ᵀ | Exact |
|---|---|---|---|
| aspect +1.0 | **1.0000** | 0.2656 | 0 |
| aspect −1.0 | **1.0000** | 0.1721 | 0 |
| keystone V 0.45 + H 0.30 | 0.5825 | 0.2188 | 0 |
| keystone V 1.00 | 0.9548 | 0.3002 | 0 |
| neutral | 0.0000 | 0.0000 | 0 |

1.0000 is not a rim softness: it is a pixel the render covers completely and the
interface draws clear, or the reverse. It is red in the instrument the repository
already has — `maskcheck 20 -2.0` on the **shipping** build, that ramp under
`perspectiveAspect 1.0`, reports **3 of 27 clear cells leaked, worst 0.1300
luma**, and 2 of 287 covered cells that did nothing. `maskcheck 12 -2.0` is red
too. No scenario in `repro/` runs a *linear* mask under an aspect squeeze, which
is why it has never been seen.

⚠ **J⁻ᵀ alone is not the fix** — it removes about three quarters and leaves 0.27,
because the perspective divide makes t a **ratio** of linear forms rather than a
linear form. That residual is the same non-uniformity noted above: a projective
map preserves cross-ratios along a line, not ratios.

**The exact answer is closed form and cheaper than the approximation deserves.**
With M the whole frame → display map as one 3×3 — crop, straighten, quarter
turns and the homography folded together — and the ramp drawn from z along u in
display coordinates:

    t(q) = ⟨n, (q, 1)⟩ / ( |u|² · ⟨M₃, (q, 1)⟩ ),    n = uₓ·M₁ + u_y·M₂ − ⟨z, u⟩·M₃

where M₁, M₂, M₃ are M's rows. Six floats, two dot products and one divide,
against the four floats and one dot product it replaces. Verified against the
point-by-point exact answer on a 400 × 400 grid over four corrections: worst
disagreement **2.2 × 10⁻⁶**, which is float rounding.

⚠ It is exact for **every** homography, crop, straighten and quarter turn at
once, because they compose into M — so it does not merely fix the squeeze, it
removes the whole class. Costed in `ROADMAP.md`; not built here.

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
- `testPerspectiveMaskExtent` — the ellipse. An axis-aligned mask under a full
  aspect squeeze comes out stretched by g and not by √g; a mask at an angle
  comes out **turned**; every point of the source rim, mapped, lands on the rim
  of the ellipse that comes back, over a squeeze, a shear each way and a general
  2×2; the area is |det J|; and a neutral control returns the crop's answer
  **to the bit**
- `mask::lengthAlong` — four checks in `tests_mask_geom.cpp`: the identity
  returns exactly 1.0f; `diag(2, ½)` returns 2 along x and ½ along y, where
  √|det J| would answer 1 for both; and a shear `{1, ½, 0, 1}` along y returns
  √1.25, which no single matrix entry gives

`repro/`: `perspective-carries-the-mask.txt` — the control moves the picture, a
mask stays on its subject with the control up, its extent survives an aspect
squeeze, a *linear* mask's placement survives a keystone (§4b), and all three
fields survive a reopen.

⚠ **Not pinned:** the call site that reaches `lengthAlong`, in
`DevelopMask.cpp`. The fix moves the frame 0.6753 → 0.6734 luma, which no cell
classification sees; §4b says so in the file rather than leaving it to be
discovered, and the number is there for whoever decides a 0.002-margin golden
check is worth its flakiness after all.

`orion-bench`: a `perspective` control probe with a magnitude floor, per
decision #37.
