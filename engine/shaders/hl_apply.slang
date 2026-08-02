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

struct HlApplyParams
{
    uint2 size;         // the picture
    uint2 fillSize;     // the solve grid
    uint  scale;        // size / fillSize, hlfill::kSolveScale
    float clip;         // whiteClipFor(m) — the same ceiling linearize used
    float gamma;        // fraction of clip at which a channel counts as clipped
    float strength;     // 0 disables
};

[shader("compute")]
[numthreads(16, 16, 1)]
void hlApply(uint3 tid : SV_DispatchThreadID,
             uniform Texture2D<float4>   src,
             uniform Texture2D<float4>   fill,
             uniform RWTexture2D<float4> dst,
             uniform ConstantBuffer<HlApplyParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float4 source = src[tid.xy];
    const float3 c = source.rgb;
    const float limit = p.clip * p.gamma;

    // Omega^intersection, tested exactly as `hl_mask.slang` tested it. Two
    // copies of one predicate is a thing to be nervous about, and the check that
    // catches them disagreeing is that the fill would then be read at a pixel
    // the solver treated as known, where rho == f and the write is a no-op.
    const bool blown = c.r >= limit && c.g >= limit && c.b >= limit;
    if (p.strength <= 0.0f || !blown)
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

    dst[tid.xy] = float4(lerp(c, rho, p.strength), source.a);
}
