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
    float guideEpsilonUnused;
    uint2 size;
    /// Subsampled dimensions of the guide coefficients, and the ratio to this
    /// kernel's own grid. See guide_down.slang.
    uint2 guideSize;
    // Colour mixer: eight hue bands, three shifts each.
    float hueShift[8];
    float satShift[8];
    float lumShift[8];
};

/// Bilinear lift of the guide coefficients from their subsampled grid.
float2 sampleAbBilinear(Texture2D<float2> ab, uint2 at, uint2 full, uint2 small)
{
    if (small.x == 0u || small.y == 0u) return ab[at];
    if (small.x == full.x && small.y == full.y) return ab[at];

    // Pixel centres on both grids, so the lift has no half-pixel drift.
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
                   uniform RWTexture2D<float4> dst,
                   uniform ConstantBuffer<LinearAdjustParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    float3 c = src[tid.xy].rgb;

    // Reconstruct the guided-filter output: q = a*I + b, where I is the
    // unexposed log2 luminance. Exposure is a multiply, so in log2 it is an
    // add — which is why the whole guide chain stays cached while exposure
    // moves, and only this one line accounts for it.
    //
    // a and b are solved on a subsampled grid and lifted back here (He & Sun,
    // 2015). Bilinear rather than nearest: they vary smoothly by construction,
    // and a nearest lift would print the subsampling grid into every gradient.
    const float2 ab = sampleAbBilinear(guideAb, tid.xy, p.size, p.guideSize);
    const float rawEv = guideRaw[tid.xy].x;
    const float guideEv = ab.x * rawEv + ab.y + p.exposureEv;

    c = applyExposure(c, p.exposureEv);
    c = applyTone(c, guideEv, p.highlights, p.shadows, p.whites, p.blacks);
    c = applyColor(c, p.vibrance, p.saturation);
    c = applyColorMixer(c, p.hueShift, p.satShift, p.lumShift);

    dst[tid.xy] = float4(c, 1.0f);
}
