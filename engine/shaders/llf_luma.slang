// Local Laplacian, step 1 — the channel the filter actually runs on.
//
// Paris et al. §5.3 offers two ways to handle colour: remap the RGB vector,
// which also boosts colour contrast, or "treat only the luminance channel and
// reintroduce chrominance after". Their Figure 9 is the side by side, and the
// second is what their own implementation does. Orion takes it too: a clarity
// slider that moves hue is a clarity slider that fights the grading wheels.
//
// The filter runs on log luminance, as the paper does for tone manipulation —
// and as every other node in this pipeline already reasons, because the eye
// judges tone in stops. Normalized into [0, 1] over a fixed window so that
// sigmaR and the noise thresholds are stated in units of the window and mean
// the same thing on a night frame and a beach.

#include "ops/llf_ops.slang"

struct LlfLumaParams
{
    uint2 size;
    float lo;         // window floor, log2 units
    float invRange;   // 1 / (hi - lo)
};

float llfLuminance2020(float3 c)
{
    // Rec.2020's own luminance, not Paris's (20, 40, 1)/61. This is the
    // working space's definition and what guide_prep.slang already means by
    // the word; two definitions of luminance in one pipeline is a bug waiting
    // for the frame that exposes it.
    return dot(c, float3(0.2627f, 0.6780f, 0.0593f));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void llfLuma(uint3 tid : SV_DispatchThreadID,
             uniform Texture2D<float4>  src,
             uniform RWTexture2D<float> dst,
             uniform ConstantBuffer<LlfLumaParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float y = max(llfLuminance2020(src[tid.xy].rgb), 1e-8f);
    dst[tid.xy] = saturate((log2(y) - p.lo) * p.invRange);
}
