// AgX — the display transform. Exactly one of these in the pipeline, at the end.
//
// A per-channel sigmoid applied in deliberately *inset* primaries. The inset is
// the whole trick: rotating toward the achromatic axis before the curve means
// bright saturated colours desaturate toward white as they clip, instead of
// skewing hue the way a naive per-channel curve does (the "notorious six").
//
// Reference: https://github.com/sobotka/AgX

struct AgxParams
{
    float contrast;    // slope of the sigmoid at middle grey
    float pivot;       // middle grey in log2 space, typically -2.5
    float saturation;  // post-curve trim, 1.0 = untouched
    uint2 size;
};

static const float kMinEv = -12.47393f;
static const float kMaxEv =   4.026069f;

// Rec.2020 -> AgX inset. Compresses the gamut inward so the sigmoid has room
// to roll off without leaving the spectral locus.
static const float3x3 kInset = float3x3(
    0.856627153315983f, 0.137318972929847f, 0.11189821299995f,
    0.0951212405381588f, 0.761241990602591f, 0.0767994186031903f,
    0.0482516061458583f, 0.101439036467562f, 0.811302368396859f);

static const float3x3 kOutset = float3x3(
    1.1271005818144368f, -0.1413297634984383f, -0.14132976349843826f,
   -0.11060664309660323f, 1.157823702216272f, -0.11060664309660294f,
   -0.016493938717834573f, -0.016493938717834257f, 1.2519364065950405f);

// Six-term polynomial fit of the AgX sigmoid over the normalised 0..1 range.
float agxCurve(float x)
{
    const float x2 = x * x;
    const float x4 = x2 * x2;
    return  15.5f     * x4 * x2
          - 40.14f    * x4 * x
          + 31.96f    * x4
          -  6.868f   * x2 * x
          +  0.4298f  * x2
          +  0.1191f  * x
          -  0.00232f;
}

float3 agxLook(float3 c, float saturation)
{
    const float luma = dot(c, float3(0.2126f, 0.7152f, 0.0722f));
    return luma + saturation * (c - luma);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void agx(uint3 tid : SV_DispatchThreadID,
         uniform Texture2D<float4>   src,
         uniform RWTexture2D<float4> dst,
         uniform ConstantBuffer<AgxParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    float3 c = max(src[tid.xy].rgb, 0.0f);

    c = mul(kInset, c);

    // Into log2 exposure, normalised across the transform's dynamic range.
    c = clamp(log2(max(c, 1e-10f)), kMinEv, kMaxEv);
    c = (c - kMinEv) / (kMaxEv - kMinEv);

    // Contrast pivots about middle grey so raising it does not also shift
    // overall brightness.
    const float pivotNorm = (p.pivot - kMinEv) / (kMaxEv - kMinEv);
    c = saturate((c - pivotNorm) * p.contrast + pivotNorm);

    c = float3(agxCurve(c.r), agxCurve(c.g), agxCurve(c.b));

    c = mul(kOutset, c);
    c = agxLook(c, p.saturation);

    // Display encode. sRGB transfer — swap for PQ when the EDR path lands.
    c = saturate(c);
    c = select(c <= 0.0031308f,
               c * 12.92f,
               1.055f * pow(c, 1.0f / 2.4f) - 0.055f);

    dst[tid.xy] = float4(c, 1.0f);
}
