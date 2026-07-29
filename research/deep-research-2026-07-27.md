# Deep research — 2026-07-27

Commissioned by the project owner, run through Gemini deep research. **This is
the source of truth for the four areas below.** Where anything in the other
`research/` files disagrees with this, this wins; the other file gets corrected
and the change noted in its History section.

Verbatim findings, reorganised for reference. Implementation status is marked
per section.

---

## 1. Highlight reconstruction — ⬜ not yet implemented

### Cross-channel correlation (Masood, Zhu & Tang, 2009)

Operates in **linear camera RGB**, before white balance scaling and color
transforms.

For a saturated region *S*, take a boundary neighbourhood Ω of valid pixels
adjacent to it and fit a local linear model by ordinary least squares:

```
C_c(p) = α · C_u(p) + β        for all p in Ω
```

where `C_c` is a clipped channel and `C_u` an unclipped reference channel.

```
α = ( |Ω| Σ C_u C_c − (Σ C_u)(Σ C_c) ) / ( |Ω| Σ C_u² − (Σ C_u)² )
β = mean(C_c) − α · mean(C_u)
```

Then reconstruct any clipped pixel *q* in *S* as `Ĉ_c(q) = α · C_u(q) + β`.

With two channels clipped, the single valid channel drives both fits. With all
three clipped, fall back to spatial gradient propagation across ∂S.

**Cost:** O(N²) per saturated pixel for an N×N window, N typically 7–15.
**GPU:** high. Threadgroups over tiles, parallel reduction in shared memory for
the sums, then a linear evaluation kernel.

### darktable "inpaint opposed" — ⚠️ not academically published

Developed by darktable and G'MIC contributors. Canonical definition is
`src/iop/highlights.c`, introduced in **darktable PR #12692 (2022)**, plus
pixls.us discussion. Operates in native linear camera RGB.

Let `U(p)` be the valid channels at p and `V(p)` the clipped ones. Take the mean
of the valid channels as a baseline:

```
C_ref(p) = (1/|U(p)|) Σ_{k in U(p)} C_k(p)
```

Then for each clipped channel c, propagate the *color offset* relative to that
baseline from surrounding valid pixels:

```
Ĉ_c(p) = C_ref(p) + Σ_q w(p,q)·( C_c(q) − C_ref(q) ) / Σ_q w(p,q)
w(p,q)  = exp( −‖p − q‖² / (2 σ_spatial²) )
```

This keeps the high-frequency luminance from the valid channel at p while
taking chromaticity from the valid boundary — which is why it holds detail
rather than flooding the region with flat color.

**Cost:** O(R²) per clipped pixel, radius 3–9. **GPU:** high, 2D stencil.

### Guided-laplacian (darktable 4.0, Pierre 2022)

Multi-scale B-spline Laplacian pyramid over *S* scales, on Bayer RAW linear.

```
D⁽ˢ⁾(x) = I⁽ˢ⁻¹⁾(x) − I⁽ˢ⁾(x),   I⁽⁰⁾ = I
D_c⁽ˢ⁾(x) = a⁽ˢ⁾(x) · D_g⁽ˢ⁾(x)
a⁽ˢ⁾(x)   = ( Σ_ω D_c⁽ˢ⁾ D_g⁽ˢ⁾ + ε ) / ( Σ_ω (D_g⁽ˢ⁾)² + ε )
Ĉ_c(x)    = I_c⁽ˢ⁾(x) + Σ_s D_c⁽ˢ⁾(x)
```

Dirichlet boundary conditions at unclipped edges. **Cost:** O(S·K) per pixel.

### ★ Per-channel clipping threshold — needed regardless of method

This is the part Orion currently lacks entirely.

```
T_raw,k = (W_raw − B_k) / (2^bitdepth − 1 − B_k)
T_wb,k  = T_raw,k · m_k
T_clip  = γ · min_k( T_raw,k · m_k ),    γ in [0.95, 0.99]
```

Because white balance scales red and blue **upward** relative to green,
`T_wb,R` and `T_wb,B` routinely exceed 1.0. Detecting clipping *after* white
balance without accounting for this produces false chromaticity shifts — which
is exactly the failure mode to avoid.

---

## 2. Profiled wavelet denoising — ⬜ not yet implemented

### Starlet / à-trous transform (Starck et al., 2007)

1D B-spline scaling filter:

```
h = [ 1/16, 1/4, 3/8, 1/4, 1/16 ]
```

2D separable, H₀ = hᵀh:

```
       1  4  6  4  1
       4 16 24 16  4
1/256· 6 24 36 24  6
       4 16 24 16  4
       1  4  6  4  1
```

At scale j, insert 2ʲ−1 zeros between coefficients ("with holes"). Then:

```
c_{j+1} = c_j * H_j
w_j     = c_j − c_{j+1}
I       = c_J + Σ_j w_j          (exact reconstruction)
```

Translation-invariant, which is what stops shrinkage producing ringing.

### Shrinkage

```
soft(w,τ)    = sgn(w)·max(0, |w| − τ)
hard(w,τ)    = w if |w| > τ else 0
garrote(w,τ) = w − τ²/w if |w| > τ else 0
```

Threshold per scale: `τ_j(x,y) = β_j · σ_j(x,y)`, with `β_j = β₀ · 2^(−j/2)`
or BayesShrink.

### Poisson–Gaussian sensor noise

```
σ²(x) = a·x + b
```

`a·x` is photon shot noise (signal-dependent), `b` is read noise, dark current
and amplifier noise (constant). Fitted per camera and ISO by weighted least
squares over intensity bins.

Mapping to scales — the à-trous filter attenuates noise at coarser scales, so:

```
σ_j(x,y) = ‖W_j‖₂ · sqrt( a·x(x,y) + b )
```

**‖W_j‖₂ for the 5×5 starlet, scales 0–4:**
`[0.8907, 0.2007, 0.0855, 0.0412, 0.0202]`

### Generalised Anscombe transform (optional)

```
f(x)    = (2/a)·sqrt( a·x + b + (3/8)a² )      when the radicand > 0
f⁻¹(y)  = (1/a)(y/2)² + b/a − 1/(8a) − (1/(4a))·sqrt(3/2)·y⁻¹ + O(y⁻²)
```

Stabilises to σ ≈ 1, after which `τ_j = β_j · ‖W_j‖₂`. darktable supports both
this and direct per-pixel variance evaluation.

### darktable noise profiles

Published as open JSON in `noiseprofiles.json`, keyed by EXIF make/model and
ISO, with per-channel `a` and `b`. Generated by `darktable-gen-noiseprofile`.
**Directly usable** — no need to measure our own for supported cameras.

---

## 3. Tone controls — 🟡 structure correct, constants to be replaced

Orion's guided filter and local/pointwise split are already right. **The mask
geometry below supersedes the invented constants** currently in
`ops/tone_ops.slang`.

`EV(Y) = log₂(Y / 0.18)`

| Control | Center μ | Active range | Knee band |
|---|---|---|---|
| Blacks | −5.5 EV | −∞ … −4.0 | −4.5 … −3.5 |
| Shadows | −2.5 EV | −4.0 … −1.0 | −1.5 … −0.5 |
| Highlights | +2.5 EV | +1.0 … +4.0 | +0.5 … +1.5 |
| Whites | +5.5 EV | +4.0 … +∞ | +3.5 … +4.5 |

### ★ Partition of unity — the part Orion gets wrong

```
w_k(EV) = exp( −(EV − μ_k)² / (2σ_k²) )
ŵ_k(EV) = w_k(EV) / Σ_j w_j(EV)
```

Normalizing so the weights **sum to 1** is what prevents double-counting where
bands overlap. Orion currently multiplies independent smoothstep masks, which
double-counts in the overlap and produces brightness spikes.

### Slider mapping

```
ΔEV_k = (s_k / 100) · EV_max_range,     EV_max_range ≈ 2.0 EV
g(x)  = 2^( Σ_k ŵ_k(EV(x)) · ΔEV_k )
```

A single summed exponent rather than multiplied gains — again, this is what
keeps overlapping bands from compounding.

### Local versus pointwise, with the reason

```
log I(x) = B(x) + D(x)
log I_out(x) = f_gain(B(x)) + D(x)
```

Highlights and shadows act on the **base layer only**, leaving detail `D(x)`
untouched — that is what preserves micro-contrast and avoids halos. Whites and
blacks set the display endpoints and must stay global and monotonic; filtering
them spatially would produce halos along extreme contrast boundaries such as a
dark window frame against bright sky.

This confirms Orion's existing split as correct.

### Reference implementations

- **Adobe PV2012** — multi-scale edge-aware decomposition for highlights and
  shadows; whites and blacks as global monotonic anchors.
- **darktable tone equalizer** — B-spline guided filter drives a 5–9 node
  Hermite spline over EV.
- **darktable filmic RGB** (Pierre) — piecewise polynomial spline with C¹
  continuity, mapping user white/black EV limits to display targets.
- **Lischinski et al. (2006)** — energy minimisation in the gradient domain,
  solving `Δf = ∇·∇g`.
- **Farbman et al. (2008)** — weighted least squares, `(I + λL_g)a = g`.

---

## 4. Lens corrections — ✅ implemented (distortion, TCA, vignetting, autoscale)

Lensfun conventions. `R_norm = ½·sqrt(width² + height²)`, `r_u` normalized
radius from the optical center.

### Distortion

```
poly3:   r_d = r_u · ( (1 − k₁) + k₁·r_u² )
poly5:   r_d = r_u · ( 1 + k₁·r_u² + k₂·r_u⁴ )
ptlens:  r_d = a·r_u⁴ + b·r_u³ + c·r_u² + (1 − a − b − c)·r_u
```

`d = 1 − a − b − c` enforces `r_d(1) = 1` at the frame boundary. Remap:

```
x_d = x₀ + (x_u − x₀)·(r_d / r_u)
```

### Autoscale — keeping the corrected picture in the frame

`d = 1 − a − b − c` pins `r_d(1) = 1`, so the frame **corners** stay put. That
is not the same as the frame staying full: `r = 1` is the corner, and a frame is
a rectangle. The edge midpoints sit at `r = W / sqrt(W² + H²)` — 0.83 on 3:2 —
where poly3 with a negative `k₁` gives a multiplier of `1 + 0.31·|k₁|`. Those
pixels are fetched from outside the image, and an edge-clamped sampler returns
the border pixel for every one of them: a band of one column smeared sideways.
At Orion's slider maximum (`k₁ = −0.35`) that band is 325 px on a 6024 px frame.

lensfun answers this with `lf_modifier_get_auto_scale`, which searches the
destination frame for the largest overreach and returns the zoom that brings it
back to the edge; darktable exposes it as "auto scale" and Lightroom as the
constrained crop. Orion computes the same quantity in `pipe/LensGeometry.h`:

```
find the largest s ≤ 1 such that, for every pixel on the destination perimeter,
    | (d·s) · m(|d·s| / R_norm) |  ≤  half the frame, on both axes
```

where `m` is the radial multiplier including the widest of the three channel
scales. Bisection on `s` is valid because the fetch distance along a ray is
`t·m(t)`, whose derivative `(1 − k₁) + 3·k₁·t²/R²` is positive throughout for
|k₁| < ½ — so the distance grows monotonically outward and the perimeter bounds
the interior. Pincushion (`k₁ > 0`) pulls samples inward and returns exactly 1.

**Sources:** lensfun `lf_modifier_get_auto_scale` (LGPL — the *behavior* is
described here and reimplemented from the geometry above; no code was read or
copied). Same correction in darktable's lens module and Adobe's lens profile
model. The monotonicity argument and the perimeter bound are derived here.

### Transverse chromatic aberration

Green is the reference; red and blue are rescaled radially:

```
r_R = r_G · ( v_R + k_R·r_G² )
r_B = r_G · ( v_B + k_B·r_G² )
```

### Vignetting

```
V(r) = 1 + p_a·r² + p_b·r⁴ + p_c·r⁶
I_corrected = I_raw / V(r)
```

### Parameter interpolation

Interpolate in the domains the optics are actually linear in, not the raw
values: **log₂(focal length)**, **2·log₂(f-number)**, and **reciprocal focus
distance** (diopters). Bilinear or monotonic cubic Hermite over the grid cell;
monotonic splines prevent overshoot between calibrated points.

---

## Implementation priority

1. **Partition-of-unity tone masks** — replaces invented constants with
   published geometry, and fixes a real double-counting bug. Cheapest win.
2. **Highlight reconstruction**, inpaint-opposed. Needs the per-channel clip
   threshold derivation first.
3. **Profiled wavelet denoise** — darktable's noise profiles are directly usable.
4. **Lens corrections** — mechanical once the lensfun database is wired up.
