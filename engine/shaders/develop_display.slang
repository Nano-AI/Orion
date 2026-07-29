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
    float contrast;     // slope of the sigmoid at middle gray
    float pivot;        // middle gray in log2, typically -2.5
    uint  curveIdentity;// skip the LUT entirely when the curve is untouched
    uint  resolution;   // LUT entries per row
    uint2 size;
    uint  dither;       // nonzero when this node writes eight bits
    uint  _pad;
};

/// Ordered dithering (Bayer, 1973), 4x4.
///
/// Eight bits is all the drawable holds, so quantising here costs nothing on
/// its own — but the geometry node then *resamples* these values and quantises
/// again, and two roundings of a smooth gradient is exactly where contouring
/// comes from. A night sky at 200% is the case that shows it. A sub-LSB offset
/// that varies with position turns the contour into noise the eye integrates
/// away; it is what every eight-bit display pipeline does, and it is free.
static const float kBayer4x4[16] = {
     0.0f,  8.0f,  2.0f, 10.0f,
    12.0f,  4.0f, 14.0f,  6.0f,
     3.0f, 11.0f,  1.0f,  9.0f,
    15.0f,  7.0f, 13.0f,  5.0f,
};

static const float kMinEv = -12.47393f;
static const float kMaxEv =   4.026069f;

// Working space is linear Rec.2020; AgX is defined against Rec.709 primaries,
// so convert first. Skipping this step is not a subtle error — it silently
// rotates every hue.
static const float3x3 kRec2020ToRec709 = float3x3(
     1.6605f, -0.5876f, -0.0728f,
    -0.1246f,  1.1329f, -0.0083f,
    -0.0182f, -0.1006f,  1.1187f);

// AgX inset and outset, from Troy Sobotka's reference implementation.
//
// Every row sums to 1, which is the property that matters: it means neutral
// gray stays neutral. An earlier pair of matrices here did not have it — the
// outset mapped (1,1,1) to roughly (0.84, 0.94, 1.22), lifting blue well above
// green and casting the whole image purple. The test in orion-tests asserts
// neutrality now, so that cannot recur silently.
static const float3x3 kInset = float3x3(
    0.84247906f, 0.07843360f, 0.07922375f,
    0.04232824f, 0.87846864f, 0.07916613f,
    0.04237565f, 0.07843360f, 0.87914297f);

static const float3x3 kOutset = float3x3(
     1.19687900512017f,   -0.0980208811401368f, -0.0990297440797205f,
    -0.0528968517574562f,  1.15190312990417f,   -0.0989611768448433f,
    -0.0529716355144438f, -0.0980434501171241f,  1.15107367264116f);

// Six-term polynomial fit of the AgX sigmoid over the normalized range.
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
    c = max(mul(kRec2020ToRec709, c), 0.0f);
    c = mul(kInset, c);

    c = clamp(log2(max(c, 1e-10f)), kMinEv, kMaxEv);
    c = (c - kMinEv) / (kMaxEv - kMinEv);

    // Contrast pivots about middle gray, so raising it does not also shift
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
    // what washed the image out: middle gray landed at 0.74 instead of 0.50,
    // lifting everything and flattening color along with it.
    if (p.dither != 0u)
    {
        const uint i = (tid.y & 3u) * 4u + (tid.x & 3u);
        c += (kBayer4x4[i] / 16.0f - 0.5f) / 255.0f;
    }

    dst[tid.xy] = float4(saturate(c), 1.0f);
}
