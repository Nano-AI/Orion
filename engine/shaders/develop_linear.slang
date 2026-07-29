// Every scene-linear adjustment, in one pass.
//
// Exposure, the four tone controls, and vibrance/saturation are all pointwise,
// so running them as separate kernels would move 194 MB in and out per stage at
// 24 MP for no benefit. Each operation still lives in its own function in
// ops/tone_ops.slang; this kernel just applies them in order.
//
// Pipeline position: after the camera matrix, before the display transform.
// Everything here is unbounded linear Rec.2020.

#include "ops/tone_ops.slang"
#include "ops/hsl_ops.slang"

struct LinearAdjustParams
{
    float exposureEv;
    float highlights;
    float shadows;
    float whites;
    float blacks;
    float vibrance;
    float saturation;
    /// Nonzero when the guided-filter chain is live. See below.
    float guideEnabled;
    uint2 size;
    /// Subsampled dimensions of the guide coefficients, and the ratio to this
    /// kernel's own grid. See guide_down.slang.
    uint2 guideSize;
    // Color mixer: eight hue bands, three shifts each.
    float hueShift[8];
    float satShift[8];
    float lumShift[8];

    /// A local adjustment, and whether there is a mask to scale it by.
    ///
    /// research/masking.md section 2: the alpha multiplies the **parameter**,
    /// not two rendered results. For exposure that is literally
    /// `x * 2^(alpha * stops)` — a smooth multiplicative ramp in linear light,
    /// artifact-free by construction. Blending two renders instead would mix
    /// two tone-mapped states and shift hue across the transition band.
    float localExposureEv;
    float maskActive;

    /// Paint the coverage on screen instead of only acting through it.
    ///
    /// A mask you cannot see is a mask you cannot place, and it is the only way
    /// to tell "the brush did nothing" from "the brush painted somewhere else".
    /// Never set while exporting — this is a viewing aid, not an edit.
    float maskOverlay;
};

/// Bilinear lift of the guide coefficients from their subsampled grid.
float2 sampleAbBilinear(Texture2D<float2> ab, uint2 at, uint2 full, uint2 small)
{
    if (small.x == 0u || small.y == 0u) return ab[at];
    if (small.x == full.x && small.y == full.y) return ab[at];

    // Pixel centers on both grids, so the lift has no half-pixel drift.
    const float2 scale = float2(small) / float2(full);
    const float2 q = (float2(at) + 0.5f) * scale - 0.5f;

    const float2 maxCoord = float2(small) - 1.0f;
    const float2 clamped = clamp(q, float2(0.0f, 0.0f), maxCoord);

    const int2 i0 = int2(floor(clamped));
    const int2 i1 = min(i0 + int2(1, 1), int2(maxCoord));
    const float2 f = clamped - float2(i0);

    const float2 a = lerp(ab[int2(i0.x, i0.y)], ab[int2(i1.x, i0.y)], f.x);
    const float2 b = lerp(ab[int2(i0.x, i1.y)], ab[int2(i1.x, i1.y)], f.x);
    return lerp(a, b, f.y);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void developLinear(uint3 tid : SV_DispatchThreadID,
                   uniform Texture2D<float4>   src,
                   uniform Texture2D<float2>   guideAb,   // a, b from the guided filter
                   uniform Texture2D<float2>   guideRaw,  // log2 luminance
                   uniform Texture2D<float>    mask,      // local alpha, 0..1
                   uniform RWTexture2D<float4> dst,
                   uniform ConstantBuffer<LinearAdjustParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    float3 c = src[tid.xy].rgb;

    c = applyExposure(c, p.exposureEv);

    // The neighbourhood's EV, which is what the highlight and shadow bands are
    // weighted by.
    //
    // The guide chain is seven nodes and only these two bands use it, so it is
    // switched off when both sliders are zero. A disabled node resolves to the
    // last live producer upstream — the color matrix — so these two bindings
    // then carry ordinary linear RGB. Sampling them regardless read that RGB as
    // log2 luminance and as filter coefficients: `rawEv` became a linear value
    // and `ab` became the pixel's own red and green.
    //
    // The offsets were zero, so nothing moved directly. But the weights are
    // normalized into a partition of unity, and two of the four came from that
    // garbage — so they sat in the denominator and diluted whites and blacks by
    // an amount that varied per pixel with the pixel's own color. A blacks-only
    // edit was quietly weaker than it asked to be, everywhere, and the bench
    // printed ok because it only ever asserted that *something* changed.
    //
    // With no neighbourhood estimate the honest answer is the pixel itself,
    // which is also what makes the fallback exact: the bands then read the same
    // EV whites and blacks do.
    float guideEv;
    if (p.guideEnabled != 0.0f)
    {
        // Reconstruct the guided-filter output: q = a*I + b, where I is the
        // unexposed log2 luminance. Exposure is a multiply, so in log2 it is an
        // add — which is why the whole guide chain stays cached while exposure
        // moves, and only this one line accounts for it.
        //
        // a and b are solved on a subsampled grid and lifted back here (He &
        // Sun, 2015). Bilinear rather than nearest: they vary smoothly by
        // construction, and a nearest lift would print the subsampling grid
        // into every gradient.
        const float2 ab = sampleAbBilinear(guideAb, tid.xy, p.size, p.guideSize);
        const float rawEv = guideRaw[tid.xy].x;
        guideEv = ab.x * rawEv + ab.y + p.exposureEv;
    }
    else
    {
        guideEv = log2(max(luminance2020(c), 1e-6f));
    }

    c = applyTone(c, guideEv, p.highlights, p.shadows, p.whites, p.blacks);
    c = applyColor(c, p.vibrance, p.saturation);
    c = applyColorMixer(c, p.hueShift, p.satShift, p.lumShift);

    // The local exposure, scaled by coverage. One multiply, no blend.
    if (p.maskActive != 0.0f && p.localExposureEv != 0.0f)
    {
        c *= exp2(saturate(mask[tid.xy]) * p.localExposureEv);
    }

    // The coverage, drawn over the picture. Last, so it sits on top of every
    // adjustment, and in scene-linear so it goes through the same tone
    // transform the photograph does rather than being pasted on afterwards.
    //
    // Some of the image is kept underneath rather than flooding flat red: you
    // are placing the mask *against* the subject, so the subject has to stay
    // legible through it. The constant floor is what keeps the overlay visible
    // in deep shadow, where a purely proportional tint would vanish exactly
    // where a photographer most wants to check coverage.
    if (p.maskOverlay != 0.0f && p.maskActive != 0.0f)
    {
        const float a = saturate(mask[tid.xy]);
        const float3 red = float3(0.35f, 0.012f, 0.02f);
        c = lerp(c, red + c * 0.25f, a * 0.6f);
    }

    dst[tid.xy] = float4(c, 1.0f);
}
