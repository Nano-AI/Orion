// Dehaze, last step — Eq. (16), the scene radiance.
//
//     J(x) = (I(x) - A) / max(t(x), t0) + A          t0 = 0.1
//
// The floor on t is the paper's: transmission goes to zero at the horizon and
// the division would not survive it. The paper also notes the result "looks
// dim" and that they raise the exposure of J for display — Orion does not,
// deliberately. The exposure slider is right there, it is scene-referred, and a
// hidden gain inside a haze control would be a second exposure control that
// nobody can see.
//
// The transmission is reconstructed here rather than read: the guided filter's
// coefficients vary slowly by construction, so they are solved subsampled and
// lifted bilinearly (He & Sun, "Fast Guided Filter"). Bilinear, not nearest —
// a nearest lift prints the subsampling grid into every gradient.

struct DehazeRecoverParams
{
    uint2 size;
    uint2 coeffSize;
    float t0;
    float lo;
    float invRange;
    float _pad;
    float4 airlight;
};

float dehazeGuideOf(float3 c, float lo, float invRange)
{
    const float y = max(dot(c, float3(0.2627f, 0.6780f, 0.0593f)), 1e-8f);
    return saturate((log2(y) - lo) * invRange);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void dehazeRecover(uint3 tid : SV_DispatchThreadID,
                   uniform Texture2D<float4>   src,
                   uniform Texture2D<float2>   coeffs,
                   uniform RWTexture2D<float4> dst,
                   uniform ConstantBuffer<DehazeRecoverParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float4 c = src[tid.xy];

    // Bilinear lift of (a, b) from the subsampled grid.
    const float2 scale = float2(float(p.coeffSize.x) / float(p.size.x),
                                float(p.coeffSize.y) / float(p.size.y));
    const float2 f = (float2(tid.xy) + 0.5f) * scale - 0.5f;
    const int2   i0 = int2(floor(f));
    const float2 w  = f - float2(i0);
    const int2   hi = int2(p.coeffSize) - 1;

    const float2 c00 = coeffs[clamp(i0,               int2(0, 0), hi)];
    const float2 c10 = coeffs[clamp(i0 + int2(1, 0),  int2(0, 0), hi)];
    const float2 c01 = coeffs[clamp(i0 + int2(0, 1),  int2(0, 0), hi)];
    const float2 c11 = coeffs[clamp(i0 + int2(1, 1),  int2(0, 0), hi)];
    const float2 ab  = lerp(lerp(c00, c10, w.x), lerp(c01, c11, w.x), w.y);

    const float g = dehazeGuideOf(c.rgb, p.lo, p.invRange);
    const float t = clamp(ab.x * g + ab.y, p.t0, 1.0f);

    const float3 j = (c.rgb - p.airlight.rgb) / t + p.airlight.rgb;

    // The model cannot produce negative radiance; arithmetic on a frame whose
    // A was mis-estimated can. Clamping here keeps that a local error rather
    // than one that propagates as a color cast through everything downstream.
    dst[tid.xy] = float4(max(j, float3(0.0f, 0.0f, 0.0f)), c.a);
}
