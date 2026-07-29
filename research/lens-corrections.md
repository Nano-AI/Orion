# Lens corrections — the models, and where the numbers come from

**Where:** `shaders/lens.slang` (one sampling pass), `pipe/LensGeometry.h`
(autoscale), `pipe/LensDatabase.{h,cpp}` (the measured coefficients),
`data/lensfun/` (the database itself).

## Source

**lensfun** — the models are defined in its manual and its database schema, and
its database is the measured data every open-source raw converter uses.

- Model definitions: <https://lensfun.github.io/manual/latest/>
- Database and licence: <https://github.com/lensfun/lensfun>

Orion implements the published models. It does **not** link the library.

## The models

    ptlens    r_d = r_u · ( a·r_u³ + b·r_u² + c·r_u + (1 − a − b − c) )
    poly3     r_d = r_u · ( (1 − k₁) + k₁·r_u² )        — ptlens with a = c = 0
    pa (vig)  V(r) = 1 + k₁·r² + k₂·r⁴ + k₃·r⁶,  corrected = raw / V(r)
    tca       r_R = r_G · ( v_R + k_R·r_G² ),  green is the reference

`r` is normalized against R_norm = ½·√(w² + h²) — lensfun's convention, and the
one every published coefficient assumes. Get that wrong and every number in the
database is scaled by the frame's aspect ratio.

The constant term in ptlens is what pins `r_d(1) = 1`: the frame corners stay
put and only the interior moves. Without it the picture scales as the
coefficients change, which reads as a zoom rather than a correction.

**One kernel, one model.** poly3 is stored as ptlens with `a` and `c` zero, so
the shader has one polynomial and the manual distortion slider — which drives
`b` alone — is exactly poly3. The 5,540 ptlens and 4,625 poly3 calibrations in
the database both land in the same three floats.

`poly5` and `linear` exist in five entries between them and are skipped.
Approximating them with a model they are not would put a wrong correction under
a label that says "measured".

## Why the data and not the library

Linking lensfun would add an LGPL-3 dependency, a build step, and a second
implementation of polynomials Orion already evaluates on the GPU — to obtain a
number that is sitting in an XML file. The maths was never the missing part.

| | Link lensfun | Read the database |
|---|---|---|
| New dependency | LGPL-3 library | none |
| Correction maths | duplicated | already written and tested |
| Data licence | CC BY-SA 3.0 either way | CC BY-SA 3.0 |
| Failure mode | version skew between library and models | a lens is not found |

⚠️ **The database is CC BY-SA 3.0.** It is vendored under `data/lensfun/`
exactly as published, with its licence file beside it. Attribution and
share-alike apply to the *data* — Orion's code is unaffected, and the copy is
unmodified. 1,558 lenses.

## Matching an EXIF name to a database entry

The two rarely agree on spelling: `FE 24-70mm F2.8 GM` against
`Sony FE 24-70mm f/2.8 GM`. Names are normalized — lowercased, everything that
is not a letter or digit dropped — which collapses the punctuation, the `f/`
and the maker prefix's separator at once while leaving the numbers intact.

A match is containment in either direction, longest wins. Two guards, because a
**confident wrong profile is worse than none** — it distorts the frame and
reports that it measured it:

- names shorter than eight characters never match (`50mm` is inside a hundred
  entries and identifies none)
- when the matched name is not the EXIF name, the profile is flagged
  `approximate` and the interface says so

The developer's own lens — Sigma 24mm F1.4 DG DN | Art 022 — is **not** in the
database. The DSLR `24mm f/1.4 DG HSM` is a different optical design, and the
test suite asserts that one never stands in for the other.

## Interpolation, and what is simplified

| Axis | Database has | Orion does |
|---|---|---|
| Focal length | 2–10 calibrations per lens | linear interpolation between the bracketing pair |
| Aperture (vignetting) | every stop | nearest calibrated stop |
| Focus distance (vignetting) | 0.25 m … 1000 m | the largest, i.e. infinity |

⚠️ All three are simplifications, stated rather than implied:

- lensfun interpolates **cubically in a transformed variable**. Calibrations are
  dense in focal length and the coefficients vary smoothly, so linear is far
  below what the correction itself is worth — but it is not what lensfun does.
- Aperture is **nearest, not interpolated**, because falloff changes fast
  between wide open and one stop down, and a value between two stops is not a
  value at either.
- Distance is fixed at infinity because a landscape or a portrait is nearer that
  end than the macro end. A close-up gets a slightly weak correction.

## Chromatic aberration stays manual

The database's TCA figures are per-copy — sample variation between two lenses of
the same model is of the same order as the aberration. A wrong one *adds*
coloured edges. The sliders are what the interface offers, and the panel says
why.

## Autoscale — the part that is not in any model

poly3 and ptlens both pin `r_d(1) = 1`, so the corners stay put. But r = 1 is
the *corner*, and a frame is a rectangle: edge midpoints sit at r ≈ 0.83 on a
3:2 frame, where a barrel correction reaches past the border. Measured on
`_PIC8148.ARW` at k₁ = −0.35: **325 px past a 6024 px frame**, and the sampler's
clamp turned that into a band of one column smeared sideways.

`pipe/LensGeometry.h` walks the destination perimeter and bisects for the zoom
that brings the worst fetch back to the edge — which is what
`lf_modifier_get_auto_scale`, darktable's auto scale and Lightroom's constrained
crop all do. It must evaluate **the same polynomial the shader does**, or the
scale is right for a transform nobody performs; both are written out in full
next to a comment saying so.

## What is pinned

`orion-tests`:

- the database parses, and it is the whole database (>1000 lenses)
- a Sony zoom is found from the EXIF spelling, and it is the right lens
- the correction changes with focal length across a zoom
- an empty name, a too-short name and an invented lens all find nothing
- a DG DN lens never matches a DG HSM entry
- on the GPU: `a`, `b` and `c` each pin the corner and each move the interior;
  the r⁴ and r⁶ vignetting terms each reach the corner. `b` and p_a were the
  only ones a manual control could ever set, so the rest would otherwise ship
  untested and arrive with the first real profile.


## Vignetting interpolates across aperture — corrected 2026-07-29

Orion used to take the **nearest calibrated aperture** and interpolate only in
focal length within it, on the reasoning that falloff changes fast between wide
open and a stop down, so a value between two stops is not a value at either.

**lensfun disagrees, and it is the authority.**
`lfLens::InterpolateVignetting` interpolates aperture rather than selecting it,
and does so against **1/aperture** — inverse-distance weighted over three axes
(focal linear, 1/aperture, 1/distance). The reciprocal is the right variable:
the f-number is a ratio, vignetting tracks the entrance pupil, and equal steps
in 1/N are equal steps in what the lens is doing.

The old behaviour is invisible by inspection and obvious once measured: a lens
calibrated at f/2 and f/2.8 rendered **every aperture between them
identically**, then jumped. Measured on the smc Pentax-FA 50mm f/1.4, which is
calibrated at eight stops:

| Aperture | p_a |
|---|---|
| f/2 (calibrated) | −0.0668 |
| **f/2.4 (interpolated)** | **−0.0904** |
| f/2.8 (calibrated) | −0.1072 |

Orion interpolates linearly between the two bracketing stops in the reciprocal,
which is the one-dimensional case of lensfun's inverse-distance weighting.

⚠️ **Focus distance is still fixed at infinity.** lensfun interpolates it too,
also reciprocally, and Orion does not — EXIF focus distance is frequently absent
and the database's distance coverage is thin. Recorded rather than implied.

⚠️ **A test that assumed the database is uniformly populated measured
nothing.** The first version of this check used the Sony FE 24-70mm F2.8 GM,
which has distortion coefficients but **no vignetting data at all** — every
aperture returned zero and the test passed its own comparison trivially. The
database is not uniformly populated, and a lens being present is not the same as
a lens being calibrated for what you are about to ask it.
