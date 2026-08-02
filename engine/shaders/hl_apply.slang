// The harmonic fill, put back into the picture — piece 3's last node.
//
// research/highlight-reconstruction.md. `hl_mask.slang` wrote the hole,
// `hl_pull`/`hl_push` solved Rouf, Lau & Heidrich's (PROCAMS 2012) §3.2
//
//     grad^2 rho = 0   over Omega,   rho|dOmega = f|dOmega
//
// on a grid `scale` times coarser than the picture. This lifts rho back and
// writes it where every channel was clipped.
//
// ⚠ **rho is written directly, not blended toward the capture's level.** §3.2's
// boundary condition is rho|dOmega = f|dOmega, so rho is already continuous with
// the picture across the rim — no seam, and no Mach band from this node. What it
// does leave is a *plateau*: a harmonic function with a roughly circular rim is
// nearly flat inside, so a blown lamp comes back with its rim's color and none
// of its falloff. That is honest and it is visible, and Rouf et al. answer it in
// §3.4 with two further solves in log space. Piece 5. Rescaling rho here to
// imitate a falloff would be inventing the shape their paper derives.
//
// ⚠ **No max(capture, rho) guard, deliberately** — which is the one
// `highlights.slang` uses, and refusing it here needs a reason. There, the
// estimate comes from an extrapolated least-squares fit that can run away, so
// clamping it to "at least what the sensor read" makes a bad fit harmless. Here
// the maximum principle already bounds rho inside the range of the rim it read
// (`hl_push.slang` carries the proof), so it cannot run away. And the guard
// would defeat the node: a fully blown pixel is (clip, clip, clip), while a rim
// color has channels *below* clip by definition of being a color — so the max
// would pin every one of them back to clip and hand back the neutral plate this
// node exists to replace.
//
// The picture is the first input, so a disabled node resolves straight through
// and the whole chain costs its textures and none of its time at strength zero.
//
// ── §3.3 rides in this node, and does not get one of its own ─────────────────
//
// Decision #109. Rouf et al.'s §3.3 ties a clipped channel to an unclipped one
// through the interpolated hue, f*_k = (rho_k/rho_j)*f_j, and integrates it by a
// Poisson solve. Two departures, both stated rather than implied:
//
// ⚠ **The Poisson integration is dropped, and the pointwise model kept.** A
// pull-push pyramid solves Laplace — it interpolates known values into a hole
// and has no residual and no relaxation, so there is nowhere to put §3.3's
// source term div g*. Writing f* = r*f_j + u gives grad^2 u = f_j grad^2 r +
// grad r . grad f_j, which vanishes only where the hue ratio r is constant; the
// neglected term is of the size of the detail being transferred. A real V-cycle
// is a different chain with a relaxation kernel per level. What is built is
// therefore §3.3's *model* with its integration replaced by a per-pixel clamp,
// and `research/highlight-reconstruction.md` §7 says so.
//
// ⚠ **It is applied only where the window fit declined.** Masood et al.'s fit is
// a measurement and this is an interpolant, so where both are available the
// measurement wins — piece 2's rule, now held by a predicate instead of by a
// comment. A channel the node lifted has `rec > raw` and is left exactly alone.
//
// ⚠ **And it reopens decision #29 differently from #106, so it owes a different
// argument.** #106 leans on the maximum principle: rho cannot leave the range of
// the rim it read, so a neutral rim gives a neutral core. That does not cover
// this branch, because f*_k = (rho_k/rho_j)*f_j multiplies a rim-derived ratio
// by the pixel's *own* measured channel and can exceed the rim's range in level.
// What is bounded is the **chromaticity**: rho is a convex combination of known
// colors, so rho_k/rho_j is a ratio the rim exhibited, and the level comes from
// evidence at that pixel rather than from anywhere else. A neutral rim gives
// rho_k = rho_j and hence f*_k = f_j, which is neutral — #29's outcome, reached
// again by evidence. The clip itself is untouched.

struct HlApplyParams
{
    uint2 size;         // the picture
    uint2 fillSize;     // the solve grid
    uint  scale;        // size / fillSize, hlfill::kSolveScale
    float clip;         // whiteClipFor(m) — the same ceiling linearize used
    float gamma;        // fraction of clip at which a channel counts as clipped
    float strength;     // 0 disables
};

/// How far above the ceiling a reconstructed channel may reach.
///
/// `highlights.slang`'s `kMaxGain`, at its value and for its reason: real
/// highlight detail lives within a stop or two of the clip, and beyond that a
/// runaway estimate is the likelier explanation. Kept identical because the two
/// nodes are the two halves of one coverage.
static const float kMaxGain = 2.0f;

[shader("compute")]
[numthreads(16, 16, 1)]
void hlApply(uint3 tid : SV_DispatchThreadID,
             uniform Texture2D<float4>   src,    // after highlightRecover
             uniform Texture2D<float4>   fill,
             uniform Texture2D<float4>   raw,    // what highlightRecover was handed
             uniform RWTexture2D<float4> dst,
             uniform ConstantBuffer<HlApplyParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float4 source = src[tid.xy];
    const float3 c  = source.rgb;
    const float3 r0 = raw[tid.xy].rgb;
    const float limit = p.clip * p.gamma;

    // The same predicate `hl_mask.slang` classified with, from the same two
    // textures. Two copies of one predicate is a thing to be nervous about, and
    // the check that catches them disagreeing is that the fill would then be
    // read at a pixel the solver treated as known, where rho == f and the write
    // is a no-op.
    const bool3 hole = bool3(r0.r >= limit && !(c.r > r0.r),
                             r0.g >= limit && !(c.g > r0.g),
                             r0.b >= limit && !(c.b > r0.b));
    const int holes = (hole.x ? 1 : 0) + (hole.y ? 1 : 0) + (hole.z ? 1 : 0);

    if (p.strength <= 0.0f || holes == 0)
    {
        dst[tid.xy] = source;
        return;
    }

    // Bilinear lift, hand-rolled for the reason `hl_push.slang` gives: sampler
    // filtering precision is unspecified across GPU families, so an export could
    // differ from a preview by device (decision #81). Half-texel centers on both
    // grids, or the fill lands offset from the hole it filled.
    const float fx = (float(tid.x) + 0.5f) / float(p.scale) - 0.5f;
    const float fy = (float(tid.y) + 0.5f) / float(p.scale) - 0.5f;

    const int   x0 = int(floor(fx));
    const int   y0 = int(floor(fy));
    const float ax = fx - float(x0);
    const float ay = fy - float(y0);

    const int2 hi = int2(p.fillSize) - 1;
    const int  xa = clamp(x0,     0, hi.x);
    const int  xb = clamp(x0 + 1, 0, hi.x);
    const int  ya = clamp(y0,     0, hi.y);
    const int  yb = clamp(y0 + 1, 0, hi.y);

    const float4 f00 = fill[int2(xa, ya)];
    const float4 f10 = fill[int2(xb, ya)];
    const float4 f01 = fill[int2(xa, yb)];
    const float4 f11 = fill[int2(xb, yb)];

    const float4 up = lerp(lerp(f00, f10, ax), lerp(f01, f11, ax), ay);

    // Still premultiplied. A weight of zero means the pyramid never reached this
    // pixel, which happens only when the frame holds no unclipped shoulder at
    // all — there is then nothing to say about the color and saying nothing is
    // the answer.
    if (up.a <= 1e-6f)
    {
        dst[tid.xy] = source;
        return;
    }

    const float3 rho = up.rgb / up.a;

    // §3.2 — every channel is a hole, so there is no reference to scale against
    // and rho is the whole answer. Unchanged from piece 3.
    if (holes == 3)
    {
        dst[tid.xy] = float4(lerp(c, rho, p.strength), source.a);
        return;
    }

    // §3.3 — some channel arrived unclipped, so it carries this pixel's own
    // detail and rho only has to say what hue to put it at. The reference is
    // every channel that never clipped, averaged: `highlights.slang` averages
    // its reference for the same reason, that two channels carry twice the
    // signal and neither one's noise steers the answer alone.
    //
    // ⚠ A channel the window fit *recovered* is not a reference. It is an
    // estimate, and using an estimate as the denominator of a ratio would let
    // one reconstruction's error set another's scale.
    float scale = 0.0f;
    int refs = 0;
    if (r0.r < limit) { scale += c.r / max(rho.r, 1e-6f); ++refs; }
    if (r0.g < limit) { scale += c.g / max(rho.g, 1e-6f); ++refs; }
    if (r0.b < limit) { scale += c.b / max(rho.b, 1e-6f); ++refs; }

    if (refs == 0)
    {
        // Every channel clipped and at least one was lifted, so there is no
        // measurement to scale by and no full hole to fill. Saying nothing.
        dst[tid.xy] = source;
        return;
    }
    scale /= float(refs);

    // A clipped channel read *at least* its level, so the estimate can only
    // raise it — and it may not run past kMaxGain, which is where `highlights`
    // stops for the same reason.
    const float ceiling = p.clip * kMaxGain;
    float3 est = c;
    if (hole.x) est.r = clamp(max(c.r, scale * rho.r), c.r, max(ceiling, c.r));
    if (hole.y) est.g = clamp(max(c.g, scale * rho.g), c.g, max(ceiling, c.g));
    if (hole.z) est.b = clamp(max(c.b, scale * rho.b), c.b, max(ceiling, c.b));

    dst[tid.xy] = float4(lerp(c, est, p.strength), source.a);
}
