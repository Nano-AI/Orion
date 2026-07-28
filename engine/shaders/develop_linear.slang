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
    float _pad;
    uint2 size;
    uint2 _pad2;
    // Colour mixer: eight hue bands, three shifts each.
    float hueShift[8];
    float satShift[8];
    float lumShift[8];
};

[shader("compute")]
[numthreads(16, 16, 1)]
void developLinear(uint3 tid : SV_DispatchThreadID,
                   uniform Texture2D<float4>   src,
                   uniform RWTexture2D<float4> dst,
                   uniform ConstantBuffer<LinearAdjustParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    float3 c = src[tid.xy].rgb;

    c = applyExposure(c, p.exposureEv);
    c = applyTone(c, p.highlights, p.shadows, p.whites, p.blacks);
    c = applyColor(c, p.vibrance, p.saturation);
    c = applyColorMixer(c, p.hueShift, p.satShift, p.lumShift);

    dst[tid.xy] = float4(c, 1.0f);
}
