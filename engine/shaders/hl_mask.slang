// The clipping mask — research/highlight-reconstruction.md, piece 2.
//
// Rouf, Lau & Heidrich (2012), "Gradient Domain Color Restoration of Clipped
// Highlights", PROCAMS 2012, §3.1 name three sets: Omega_k where channel k is
// clipped, Omega^union over all channels, and Omega^intersection where every
// channel is. This kernel writes **Omega^intersection** as the hole of the
// pull-push solver, and the picture's own color as the Dirichlet data around it.
//
// ⚠ **Why the intersection and not the union, which is what §3.2 solves over.**
// Orion already ships the union's partial case: `highlights.slang` is Masood,
// Zhu & Tappen's cross-channel fit, and over Omega^union \ Omega^intersection it
// recovers a clipped channel from the two that are still valid — per pixel, from
// real evidence. Filling that annulus with a smooth interpolant instead would
// replace measurement with interpolation, which is strictly worse. The set that
// nothing addresses is the intersection: under decision #29 `linearize` clips
// every channel to one ceiling before demosaic, so a fully blown pixel arrives
// as (clip, clip, clip) and `highlights.slang`'s count == 3 branch is a literal
// identity on it. That is the hole this fills, and it is the one measured gap.
//
// Rouf et al.'s §3.3 does transfer detail across Omega_k, and when that lands
// it wants the union — a different mask, from a different kernel, because the
// two sets differ per channel. Writing this one with a switch for the other
// would leave a branch nothing takes, which this project has already removed
// once from `hl_pull.slang` for exactly that reason.
//
// ⚠ **Storage is premultiplied**, matching `hl_pull.slang`: rgb holds w*f and a
// holds w. That is what makes the box average below correct rather than merely
// convenient — sum(w*f) over sum(w) is the mean of the *known* colors in the
// block and ignores the hole entirely, so a coarse texel straddling a rim
// carries the rim's color at a weight saying how much of it was known. An
// un-premultiplied average would mix the hole's clipped white into it in
// proportion to its area.

struct HlMaskParams
{
    uint2 outSize;    // the solve grid
    uint2 inSize;     // the picture
    uint  scale;      // inSize / outSize, hlfill::kSolveScale
    float clip;       // whiteClipFor(m) — the SAME ceiling linearize used
    float gamma;      // fraction of clip at which a channel counts as clipped
    float shoulder;   // fraction of clip below which a pixel is not evidence
};

[shader("compute")]
[numthreads(16, 16, 1)]
void hlMask(uint3 tid : SV_DispatchThreadID,
            uniform Texture2D<float4>   src,
            uniform RWTexture2D<float4> dst,
            uniform ConstantBuffer<HlMaskParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    const float limit = p.clip * p.gamma;
    const float floor_ = p.clip * p.shoulder;

    // Box-restrict onto the solve grid. The solver runs at 1/scale because rho
    // is harmonic and has no detail to lose; the factor and its measured cost
    // live on `hlfill::kSolveScale`.
    float4 acc = float4(0.0f, 0.0f, 0.0f, 0.0f);
    const int2 hi = int2(p.inSize) - 1;

    for (uint dy = 0; dy < p.scale; ++dy)
    {
        const int sy = min(int(tid.y * p.scale + dy), hi.y);
        for (uint dx = 0; dx < p.scale; ++dx)
        {
            const int sx = min(int(tid.x * p.scale + dx), hi.x);
            const float3 c = src[int2(sx, sy)].rgb;

            // Omega^intersection: every channel at its ceiling.
            const bool blown = c.r >= limit && c.g >= limit && c.b >= limit;

            // ⚠ And the rest is not automatically evidence. This is Masood et
            // al.'s Omega — the shoulder of the saturated region, not the
            // general neighborhood — at the same fraction `highlights.slang`
            // uses and for the same reason it records there: around a small
            // light at night the neighborhood is almost all dark background, and
            // a color taken from background pixels describes the background.
            // That is what put a halo round every light in the frame once
            // already.
            //
            // It is also what makes the restriction above safe. A blown lamp's
            // partial-clip annulus can be a pixel or two wide, so a coarse texel
            // straddling it would average the annulus with the night sky and
            // drag the fill toward black. Excluding the sky leaves the coarse
            // texel carrying the annulus alone, at a lower weight.
            //
            // ⚠ The dark background becoming "unknown" does not leak one lamp
            // into another. Omega^intersection is enclosed by its own shoulder
            // ring, which is known, so the solve inside a core is bounded
            // entirely by that core's own rim — decision #96's region scoping,
            // unchanged.
            const float peak = max(c.r, max(c.g, c.b));
            const float w = (blown || peak < floor_) ? 0.0f : 1.0f;

            acc += float4(c * w, w);
        }
    }

    dst[tid.xy] = acc / float(p.scale * p.scale);
}
