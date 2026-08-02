// Harmonic fill, the push half — research/highlight-reconstruction.md.
//
// The second half of the pull-push interpolant (Gortler, Grzeszczuk, Szeliski &
// Cohen, "The Lumigraph", SIGGRAPH 1996, §3.5.1) solving Rouf, Lau & Heidrich's
// (PROCAMS 2012) §3.2 Laplace fill. `hl_pull.slang` carries the argument for
// both; this file carries the descent.
//
// Coarse-to-fine: take the level below, upsample it, and let it show through
// wherever this level's own weight is short of one.
//
// ⚠ **The blend is exactly premultiplied source-over**, and that is not a
// convenience. Writing it out, with w this level's weight and w_up the
// upsampled one:
//
//     w'   = w + (1-w)*w_up
//     w'f' = w*f + (1-w)*w_up*f_up
//
// so f' is a convex combination of f and f_up with weights w and (1-w)*w_up,
// which sum to w'. Every value in the pyramid is therefore a convex combination
// of known pixels, which gives the discrete maximum principle for free: the
// fill cannot leave the range of the rim it read. That is the guard the fixed
// window in `highlights.slang` needs three explicit clamps to get, and it is
// why a runaway fit is not a failure mode this solver has.
//
// Two consequences worth stating because they are load-bearing:
//   - Where w == 1 the result is this level's own value, bit for bit. A known
//     pixel is never touched, so the Dirichlet condition holds by construction
//     rather than by a test.
//   - Where w_up == 0 nothing is blended in. An unknown pixel with nothing
//     known anywhere below it stays unknown and stays zero, rather than being
//     pulled toward black — which is what a naive lerp against an
//     un-premultiplied f_up would do.

struct HlPushParams
{
    uint2 size;         // this (fine) level
    uint2 coarseSize;   // level + 1
};

[shader("compute")]
[numthreads(16, 16, 1)]
void hlPush(uint3 tid : SV_DispatchThreadID,
            uniform Texture2D<float4>   fine,     // pull output at this level
            uniform Texture2D<float4>   coarse,   // push output at level + 1
            uniform RWTexture2D<float4> dst,
            uniform ConstantBuffer<HlPushParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    // Bilinear synthesis, hand-rolled. A hardware sampler is refused here for
    // the reason decision #81 already recorded for the grain plate: filtering
    // precision is unspecified across GPU families, so an export could differ
    // from a preview by device.
    //
    // Half-texel centers on both grids, or the fill drifts a quarter texel per
    // level and a filled region lands offset from the hole it filled.
    const float fx = (float(tid.x) + 0.5f) * 0.5f - 0.5f;
    const float fy = (float(tid.y) + 0.5f) * 0.5f - 0.5f;

    const int   x0 = int(floor(fx));
    const int   y0 = int(floor(fy));
    const float ax = fx - float(x0);
    const float ay = fy - float(y0);

    const int2 hi = int2(p.coarseSize) - 1;
    const int  xa = clamp(x0,     0, hi.x);
    const int  xb = clamp(x0 + 1, 0, hi.x);
    const int  ya = clamp(y0,     0, hi.y);
    const int  yb = clamp(y0 + 1, 0, hi.y);

    const float4 c00 = coarse[int2(xa, ya)];
    const float4 c10 = coarse[int2(xb, ya)];
    const float4 c01 = coarse[int2(xa, yb)];
    const float4 c11 = coarse[int2(xb, yb)];

    const float4 up = lerp(lerp(c00, c10, ax), lerp(c01, c11, ax), ay);

    const float4 f = fine[tid.xy];

    dst[tid.xy] = f + (1.0f - f.a) * up;
}
