// The display transform and the look, in one pass.
//
// AgX maps scene-linear to display, then the tone curve shapes the result.
// Both are pointwise, and splitting them cost a 194 MB rgba16f round trip for
// nothing. Fusing also means AgX never has to quantise to 8-bit before the
// curve sees it — the intermediate stays in registers at full float precision.
//
// There is exactly ONE display transform in the pipeline, and this is it.

struct DisplayParams
{
    float contrast;     // slope of the sigmoid at middle grey
    float pivot;        // middle grey in log2, typically -2.5
    uint  curveIdentity;// skip the LUT entirely when the curve is untouched
    uint  resolution;   // LUT entries per row
    uint2 size;
    uint2 _pad;
};

static const float kMinEv = -12.47393f;
static const float kMaxEv =   4.026069f;

// Rec.2020 -> AgX inset. Compressing the gamut inward before the sigmoid is the
// whole trick: bright saturated colours then desaturate toward white as they
// roll off, instead of skewing hue the way a naive per-channel curve does.
static const float3x3 kInset = float3x3(
    0.856627153315983f, 0.137318972929847f, 0.11189821299995f,
    0.0951212405381588f, 0.761241990602591f, 0.0767994186031903f,
    0.0482516061458583f, 0.101439036467562f, 0.811302368396859f);

static const float3x3 kOutset = float3x3(
    1.1271005818144368f, -0.1413297634984383f, -0.14132976349843826f,
   -0.11060664309660323f, 1.157823702216272f, -0.11060664309660294f,
   -0.016493938717834573f, -0.016493938717834257f, 1.2519364065950405f);

// Six-term polynomial fit of the AgX sigmoid over the normalised range.
float agxCurve(float x)
{
    const float x2 = x * x;
    const float x4 = x2 * x2;
    return  15.5f    * x4 * x2
          - 40.14f   * x4 * x
          + 31.96f   * x4
          -  6.868f  * x2 * x
          +  0.4298f * x2
          +  0.1191f * x
          -  0.00232f;
}

// Linear interpolation between LUT entries, so 256 entries are smooth enough
// that no banding survives the 8-bit output encode.
float sampleLut(Texture2D<float> lut, float v, uint row, uint resolution)
{
    const float x = saturate(v) * float(resolution - 1u);
    const uint  i = uint(floor(x));
    const uint  j = min(i + 1u, resolution - 1u);
    return lerp(lut[uint2(i, row)], lut[uint2(j, row)], x - float(i));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void developDisplay(uint3 tid : SV_DispatchThreadID,
                    uniform Texture2D<float4>   src,
                    uniform Texture2D<float>    lut,
                    uniform RWTexture2D<float4> dst,
                    uniform ConstantBuffer<DisplayParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    float3 c = max(src[tid.xy].rgb, 0.0f);

    // ── AgX ────────────────────────────────────────────────────────────────
    c = mul(kInset, c);

    c = clamp(log2(max(c, 1e-10f)), kMinEv, kMaxEv);
    c = (c - kMinEv) / (kMaxEv - kMinEv);

    // Contrast pivots about middle grey, so raising it does not also shift
    // overall brightness.
    const float pivotNorm = (p.pivot - kMinEv) / (kMaxEv - kMinEv);
    c = saturate((c - pivotNorm) * p.contrast + pivotNorm);

    c = float3(agxCurve(c.r), agxCurve(c.g), agxCurve(c.b));
    c = saturate(mul(kOutset, c));

    // ── Tone curve ─────────────────────────────────────────────────────────
    // Row 0 is master, rows 1-3 are per-channel. Master applies first, matching
    // how every editor's curve panel behaves.
    if (p.curveIdentity == 0u)
    {
        c.r = sampleLut(lut, c.r, 0u, p.resolution);
        c.g = sampleLut(lut, c.g, 0u, p.resolution);
        c.b = sampleLut(lut, c.b, 0u, p.resolution);

        c.r = sampleLut(lut, c.r, 1u, p.resolution);
        c.g = sampleLut(lut, c.g, 2u, p.resolution);
        c.b = sampleLut(lut, c.b, 3u, p.resolution);
    }

    // No sRGB encode here. The AgX polynomial is a fit of the *display*
    // rendering transform — its output is already display-referred, so
    // applying a transfer function on top encodes the signal twice. That is
    // what washed the image out: middle grey landed at 0.74 instead of 0.50,
    // lifting everything and flattening colour along with it.
    dst[tid.xy] = float4(saturate(c), 1.0f);
}
