// The clipping mask — research/highlight-reconstruction.md, piece 2.
//
// Rouf, Lau & Heidrich (2012), "Gradient Domain Color Restoration of Clipped
// Highlights", PROCAMS 2012, §3.1 name three sets: Omega_k where channel k is
// clipped, Omega^union over all channels, and Omega^intersection where every
// channel is. This kernel writes **Omega^intersection** as the hole of the
// pull-push solver, and the picture's own color as the Dirichlet data around it.
//
// ⚠ **The hole is the part of Omega^union the window fit did not recover, and
// that is a correction to piece 2 rather than a restatement of it.** Decision
// #109; piece 2 shipped the hole as Omega^intersection, on the argument that the
// annulus is `highlights.slang`'s and replacing its per-pixel cross-channel fit
// with a smooth interpolant would be strictly worse. That argument is right and
// is unchanged — but it is about what the fill may **write**, and this kernel
// decides something different: which pixels are trustworthy **evidence**.
//
// Those are not the same set, and conflating them cost accuracy where it is
// least affordable. The pixels bounding a blown core are the innermost ring of
// the annulus, and measured on `_PIC8220`/`_PIC8095` that ring is 11,901 and
// 20,563 pixels of which the window fit hands back 58% and 69% untouched — it
// declines beyond its 12-pixel reach, and 5.6%/22.7% of the ring is beyond it.
// Piece 2 fed those still-clipped pixels to the solver as Dirichlet data, so
// every core in the frame was solved from a rim that was itself clipped.
//
// Excluding them costs nothing — no node, no texture, no level — and moves the
// boundary condition out to pixels that are either unclipped or were recovered
// from real evidence. `hl_apply.slang` still writes only where every channel is
// clipped, plus §3.3's ratio transfer where some are, so piece 2's guarantee
// stands: nothing overwrites a pixel the window fit recovered.
//
// ⚠ **"Recovered" is read off the node, not inferred from a threshold.** The
// second input is `highlights`' own input, so a channel counts as recovered when
// the node actually raised it — `rec_k > raw_k` — and as still clipped when it
// did not. A level test would need a constant to separate a lifted channel from
// a blown one, and demosaic ringing around a clipped edge puts pixels on both
// sides of any such constant.
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
            uniform Texture2D<float4>   src,   // after highlightRecover
            uniform Texture2D<float4>   raw,   // what highlightRecover was handed
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
            const float3 r0 = raw[int2(sx, sy)].rgb;

            // A channel is a hole where it arrived clipped and the window fit
            // did not lift it. Omega^intersection is the case where all three
            // are, and it is no longer the only one — see the head of the file.
            const bool3 hole = bool3(r0.r >= limit && !(c.r > r0.r),
                                     r0.g >= limit && !(c.g > r0.g),
                                     r0.b >= limit && !(c.b > r0.b));
            const bool blown = hole.x || hole.y || hole.z;

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
