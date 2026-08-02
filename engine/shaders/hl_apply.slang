// The fill put back into the picture — Rouf, Lau & Heidrich (PROCAMS 2012)
// §3.2's rho where every channel is a hole, §3.3's ratio where some channel is
// not. `research/highlight-reconstruction.md` §5b and §5c carry the argument in
// full; decisions #106 and #109. `hl_mask.slang` wrote the hole and
// `hl_pull`/`hl_push` solved grad^2 rho = 0 with rho|dOmega = f|dOmega on a grid
// `scale` times coarser; this lifts it back.
//
// ⚠ **rho is written directly on the §3.2 branch, with no max(capture, rho)
// guard.** The boundary condition makes it continuous across the rim and the
// maximum principle bounds it inside that rim's range (`hl_push.slang` proves
// it), so the guard `highlights.slang` needs would only defeat this node: a
// blown pixel is (clip, clip, clip) and a rim color has channels below clip by
// definition. What it leaves is a *plateau* — §3.4, piece 5.
//
// ⚠ **§3.3 rides in this node**; a node of its own is another full-resolution
// pass with nothing to fuse into. Three lines of #109 belong here: the Poisson
// integration is **dropped** for the pointwise model, since a pull-push pyramid
// solves Laplace and has nowhere to put a source term (`UNSOURCED.md` §28); it
// applies **only where the window fit declined**, measurement beating
// interpolant; and it reopens **#29 by a different route than #106** — the bound
// here is on *chromaticity*, rho being a convex combination of known colors,
// while the level is the pixel's own evidence, so a neutral rim gives
// rho_k = rho_j and hence f*_k = f_j.
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

/// How far above the ceiling a reconstructed channel may reach.
/// `highlights.slang`'s `kMaxGain`, at its value and for its reason: real
/// highlight detail lives within a stop or two of the clip. ⚠ It caps 0 channels
/// on either sample frame — `UNSOURCED.md` §28 records why it was kept anyway.
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
    // textures. ⚠ Spelling it as a level test instead passes all 750 engine
    // checks and overwrites 13,135 pixels of a photograph; `apps/bench` 3e is
    // where that is caught.
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
    // filtering precision is unspecified across GPU families (decision #81).
    // Half-texel centers on both grids, or the fill lands offset from its hole.
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
    // pixel — the frame holds no unclipped shoulder at all — and there is then
    // nothing to say about the color.
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
    // detail and rho only says what hue to put it at. The reference is every
    // channel that never clipped, averaged as `highlights.slang` averages its
    // own: two channels carry twice the signal.
    //
    // ⚠ `r0 < limit` and `!hole` are the *same set* here, and the mutation that
    // swaps them cannot be caught: `highlights.slang` declines per pixel, so
    // either every clipped channel was lifted — and `holes == 0` returned above —
    // or none was. The intended meaning is written; the distinction becomes real
    // only if that node ever declines per channel.
    float scale = 0.0f;
    int refs = 0;
    if (r0.r < limit) { scale += c.r / max(rho.r, 1e-6f); ++refs; }
    if (r0.g < limit) { scale += c.g / max(rho.g, 1e-6f); ++refs; }
    if (r0.b < limit) { scale += c.b / max(rho.b, 1e-6f); ++refs; }

    // Every channel clipped and at least one lifted: no measurement to scale by
    // and no full hole to fill, so say nothing.
    if (refs == 0) { dst[tid.xy] = source; return; }
    scale /= float(refs);

    // A clipped channel read *at least* its level, so the estimate can only
    // raise it. That floor binds on 150 and 645 channels of the two sample
    // frames; the ceiling above it on none.
    const float ceiling = p.clip * kMaxGain;
    float3 est = c;
    if (hole.x) est.r = clamp(max(c.r, scale * rho.r), c.r, max(ceiling, c.r));
    if (hole.y) est.g = clamp(max(c.g, scale * rho.g), c.g, max(ceiling, c.g));
    if (hole.z) est.b = clamp(max(c.b, scale * rho.b), c.b, max(ceiling, c.b));

    dst[tid.xy] = float4(lerp(c, est, p.strength), source.a);
}
