// Exposure fusion, last step — back onto the picture as one gain per pixel.
//
// The fusion ran on a quarter-resolution proxy of the luminance, so what comes
// back is a *ratio*, lifted bilinearly and multiplied onto all three channels.
// Chromaticity is untouched, which is Hessel & Morel's own choice — they enhance
// V and keep H and S — and HDR+ arrives at the same place independently,
// "copying per-pixel chroma ratios from the original linear RGB image".
//
// The strength control raises that gain to its own power. That is a lerp in
// log-gain, so strength zero is gain^0 = 1 exactly, bit for bit. No published
// parameter of this method degenerates to the identity: alpha -> 1 collapses the
// exposure factors, but the restrained-range centres contain no alpha, so the
// simulated images stay differently-clipped copies and their blend is not the
// input. See research/exposure-fusion.md.

struct FuseApplyParams
{
    uint2 size;
    uint2 proxySize;
    float slope;
    float strength;
    float maxGain;     // a clamp, in place of the paper's global renormalisation
    float _pad;
};

float fuseApplyLuma(float3 c)
{
    return max(dot(c, float3(0.2627f, 0.6780f, 0.0593f)), 1e-8f);
}

/// The inverse of fuse_proxy.slang's forward curve, in stops relative to middle
/// grey. Written as a log rather than atanh so there is no doubt what it is.
float fuseInverseEv(float t, float slope)
{
    const float x = clamp(2.0f * t - 1.0f, -0.9999f, 0.9999f);
    return 0.5f * log((1.0f + x) / (1.0f - x)) * slope;
}

float fuseSample(Texture2D<float> tex, uint2 size, float2 uv)
{
    const float2 f = uv * float2(size) - 0.5f;
    const int2 i0 = int2(floor(f));
    const float2 w = f - float2(i0);
    const int2 hi = int2(size) - 1;

    const float a = tex[clamp(i0,              int2(0, 0), hi)];
    const float b = tex[clamp(i0 + int2(1, 0), int2(0, 0), hi)];
    const float c = tex[clamp(i0 + int2(0, 1), int2(0, 0), hi)];
    const float d = tex[clamp(i0 + int2(1, 1), int2(0, 0), hi)];
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void fuseApply(uint3 tid : SV_DispatchThreadID,
               uniform Texture2D<float4>   src,
               uniform Texture2D<float>    fused,     // proxy resolution
               uniform Texture2D<float>    proxy,     // proxy resolution
               uniform RWTexture2D<float4> dst,
               uniform ConstantBuffer<FuseApplyParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float4 c = src[tid.xy];

    const float2 uv = (float2(tid.xy) + 0.5f) / float2(p.size);
    const float after  = fuseSample(fused, p.proxySize, uv);
    const float before = fuseSample(proxy, p.proxySize, uv);

    // Both ends go back through the same inverse, so the proxy curve cancels
    // out of the ratio and only the tonal *move* survives. That is what stops
    // the choice of curve from tinting or brightening the picture on its own.
    const float ev = fuseInverseEv(after, p.slope) - fuseInverseEv(before, p.slope);
    const float gain = clamp(exp2(ev), 1.0f / p.maxGain, p.maxGain);

    dst[tid.xy] = float4(c.rgb * pow(gain, saturate(p.strength)), c.a);
}
