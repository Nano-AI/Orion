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

/// The log2 pivot a contrast bends about. `develop_display` uses the same one
/// for its base contrast, and middle gray (0.18) is -2.47. Two pivots in one
/// program would make a local contrast of zero visibly different from no local
/// contrast at all near the crossover.
static const float kContrastPivot = -2.5f;

/// One layer's coverage. ⚠ A branch rather than an array index: Slang cannot
/// index a set of texture parameters, and the graph is static so which slot a
/// layer uses is only known at render.
float coverage(int slot, uint2 p,
               Texture2D<float> m0, Texture2D<float> m1,
               Texture2D<float> m2, Texture2D<float> m3)
{
    if (slot == 0) return m0[p];
    if (slot == 1) return m1[p];
    if (slot == 2) return m2[p];
    return m3[p];
}

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
    /// One layer's worth of local adjustment. All pointwise, all scaled by
    /// that layer's coverage. research/masking.md §2b names what is missing and
    /// why: white balance is applied before the demosaic and the pyramid
    /// operators are not pointwise.
    ///
    /// ⚠ Four sets, not one. A layer is a run of mask components with its own
    /// coverage, so the subject can be graded one way and the sky another.
    float layerExposureEv[4];
    float layerContrast[4];
    float layerSaturation[4];
    /// A color **cast**, not a white balance. See the note at the call site.
    float layerWarmth[4];
    float layerTint[4];
    /// The four tone bands, per layer. Same units as the globals above; the
    /// coverage scales the parameter like every other local control.
    float layerHighlights[4];
    float layerShadows[4];
    float layerWhites[4];
    float layerBlacks[4];
    /// Which of the four coverage textures carries each layer. ⚠ The graph is
    /// static, so a layer's coverage cannot be a node chosen per render — the
    /// kernel binds all four and this says which one to read.
    int   layerMask[4];
    int   layerCount;
    float maskActive;

    /// Paint the coverage on screen instead of only acting through it.
    ///
    /// A mask you cannot see is a mask you cannot place, and it is the only way
    /// to tell "the brush did nothing" from "the brush painted somewhere else".
    /// Never set while exporting — this is a viewing aid, not an edit.
    float maskOverlay;
    /// Which layer the overlay paints — the one being edited. Ignored when
    /// maskOverlay is zero.
    int   maskOverlayLayer;
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
                   uniform Texture2D<float>    mask0,     // layer coverages,
                   uniform Texture2D<float>    mask1,     // one per component
                   uniform Texture2D<float>    mask2,     // slot; `layerMask`
                   uniform Texture2D<float>    mask3,     // picks per layer
                   uniform RWTexture2D<float4> dst,
                   uniform ConstantBuffer<LinearAdjustParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    float3 c = src[tid.xy].rgb;

    c = applyExposure(c, p.exposureEv);

    // The neighborhood's EV, which is what the highlight and shadow bands are
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
    // With no neighborhood estimate the honest answer is the pixel itself,
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

    // ── The local adjustments, layer by layer ────────────────────────────
    //
    // research/masking.md §2 and §2b. The alpha multiplies the **parameter**,
    // never the result: half coverage at one stop is 2^0.5, a smooth
    // multiplicative ramp in linear light rather than a blend of two rendered
    // frames. Everything here is pointwise, which is the test for whether an
    // adjustment can be local at all.
    //
    // ⚠ **One pass over the frame, not one per layer.** Layers apply in stack
    // order inside this loop, so four layers cost four sets of arithmetic on a
    // pixel already in registers rather than four passes over 24 megapixels.
    for (int L = 0; L < p.layerCount; ++L)
    {
        const float a = saturate(coverage(p.layerMask[L], tid.xy,
                                          mask0, mask1, mask2, mask3));
        if (a <= 0.0f) continue;

        if (p.layerExposureEv[L] != 0.0f) c *= exp2(a * p.layerExposureEv[L]);

        // The tone bands. `applyTone`'s deltaEv is linear in each band, so
        // scaling the parameters by the coverage is exactly scaling the
        // exponent — the same construction the exposure above uses, never a
        // blend of two rendered frames.
        //
        // ⚠ `guideEv` is the frame's one guided estimate, computed above and
        // not per layer — highlights and shadows here read the neighborhood
        // as it stood before any local adjustment. That approximation is the
        // reason these four were once refused locally; it is now accepted and
        // recorded, because a per-layer guide chain would run the six-node
        // filter once per layer per frame.
        if (p.layerHighlights[L] != 0.0f || p.layerShadows[L] != 0.0f ||
            p.layerWhites[L] != 0.0f || p.layerBlacks[L] != 0.0f)
        {
            c = applyTone(c, guideEv,
                          a * p.layerHighlights[L], a * p.layerShadows[L],
                          a * p.layerWhites[L], a * p.layerBlacks[L]);
        }

        // Contrast, as a gain on the pixel's distance from the pivot measured
        // in stops — so it is a multiply like the exposure above and keeps the
        // color's ratios, rather than bending each channel separately and
        // pulling the hue toward whichever primary is largest.
        //
        // ⚠ The pivot is `develop_display`'s, not a new one. Middle gray is
        // -2.47 in log2 and the display transform pivots at -2.5; a second
        // pivot here would make a local contrast of zero visibly different
        // from no local contrast at all near the crossover.
        if (p.layerContrast[L] != 0.0f)
        {
            const float ev = log2(max(luminance2020(c), 1e-6f));
            c *= exp2((ev - kContrastPivot) * (a * p.layerContrast[L]));
        }

        // Saturation, toward the pixel's own luminance. Same construction the
        // global one uses, so the two agree about what gray means.
        if (p.layerSaturation[L] != 0.0f)
        {
            const float y = luminance2020(c);
            c = lerp(float3(y), c, 1.0f + a * p.layerSaturation[L]);
        }

        // ⚠ A color **cast**, not a white balance. Temperature and tint are
        // applied in `linearize`, before the demosaic, and cannot be local —
        // research/masking.md §2b. This is a per-channel multiply.
        if (p.layerWarmth[L] != 0.0f || p.layerTint[L] != 0.0f)
        {
            // ⚠ Taken *before* the multiply. The first version read the
            // luminance on both sides of the cast from the same already-cast
            // color, so the ratio was one and the renormalisation did nothing.
            const float before = luminance2020(c);

            const float w = a * p.layerWarmth[L];
            const float t = a * p.layerTint[L];
            c *= float3(1.0f + w * 0.30f,
                        1.0f + t * 0.30f,
                        1.0f - w * 0.30f);

            // Renormalized on luminance so a cast moves color without also
            // moving exposure — otherwise Warmth doubles as a brightness
            // slider and the two controls fight over the same pixels.
            const float after = luminance2020(c);
            if (before > 1e-6f && after > 1e-6f) c *= before / after;
        }
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
        // ⚠ The **selected layer**, not the union of all of them. This began
        // as the union — "where is the mask" — and the photographer's answer
        // was that with two masks placed, the toggle drowned the one being
        // edited in the other's red. Show mask now answers "where is the mask
        // I am editing"; the rest of the stack acts on the picture as always,
        // which is its own report of where it is.
        //
        // ⚠ If the selected layer's only component is hidden, its disabled
        // node resolves to the fold so far, and the overlay shows the previous
        // run's coverage — the documented run-terminator behavior.
        const int L = clamp(p.maskOverlayLayer, 0, p.layerCount - 1);
        const float a = saturate(coverage(p.layerMask[L], tid.xy,
                                          mask0, mask1, mask2, mask3));
        const float3 red = float3(0.35f, 0.012f, 0.02f);
        c = lerp(c, red + c * 0.25f, a * 0.6f);
    }

    dst[tid.xy] = float4(c, 1.0f);
}
