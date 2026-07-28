// Guided filter, step 1 — luminance and its square.
//
// Implements the guided filter of He, Sun and Tang (ECCV 2010 / PAMI 2013),
// self-guided on luminance. It is what makes highlight and shadow recovery
// *local*: the gain a pixel receives depends on its neighbourhood, not on its
// own value alone. That is the actual difference between these controls and
// mine before — Lightroom's and darktable's are local, a pointwise curve can
// never behave the same way no matter how it is tuned.
//
// Both moments are packed into one two-channel target so the box blurs that
// follow filter them together, halving the number of passes.
//
// Deliberately placed *before* exposure. Exposure is a multiply, so the guide
// simply scales with it — which means this whole chain stays cached while the
// exposure slider moves.

struct GuidePrepParams
{
    uint2 size;
    uint2 _pad;
};

float luminance2020(float3 c)
{
    return dot(c, float3(0.2627f, 0.6780f, 0.0593f));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void guidePrep(uint3 tid : SV_DispatchThreadID,
               uniform Texture2D<float4>   src,
               uniform RWTexture2D<float2> dst,
               uniform ConstantBuffer<GuidePrepParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    // Work in log2 exposure: the eye judges tone in stops, and a variance
    // computed on linear values would be dominated entirely by highlights.
    const float y = max(luminance2020(src[tid.xy].rgb), 1e-6f);
    const float l = log2(y);

    dst[tid.xy] = float2(l, l * l);
}
