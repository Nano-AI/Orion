// Tone curve, applied in display space after the display transform.
//
// The curve itself is evaluated on the CPU into a lookup table — a monotone
// cubic Hermite spline through the user's control points — and uploaded as a
// LUT texture. Doing the spline per pixel would be wasted work: the curve
// changes when a control point moves, not when a pixel does.
//
// LUT layout is kResolution x 4: row 0 is the master (RGB) curve, rows 1-3 are
// the per-channel red, green and blue curves. Master applies first, matching
// how every editor's curve panel behaves.

struct CurveParams
{
    uint2 size;
    uint  resolution;   // LUT entries per row
    uint  _pad;
};

// Linear interpolation between LUT entries, so a 256-entry table is smooth
// enough that no banding survives the 8-bit output encode.
float sampleCurve(Texture2D<float> lut, float v, uint row, uint resolution)
{
    const float x = saturate(v) * float(resolution - 1u);
    const uint  i = uint(floor(x));
    const uint  j = min(i + 1u, resolution - 1u);
    const float t = x - float(i);

    const float a = lut[uint2(i, row)];
    const float b = lut[uint2(j, row)];
    return lerp(a, b, t);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void toneCurve(uint3 tid : SV_DispatchThreadID,
               uniform Texture2D<float4>   src,
               uniform Texture2D<float>    lut,
               uniform RWTexture2D<float4> dst,
               uniform ConstantBuffer<CurveParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    float3 c = saturate(src[tid.xy].rgb);

    // Master first, then per-channel.
    c.r = sampleCurve(lut, c.r, 0u, p.resolution);
    c.g = sampleCurve(lut, c.g, 0u, p.resolution);
    c.b = sampleCurve(lut, c.b, 0u, p.resolution);

    c.r = sampleCurve(lut, c.r, 1u, p.resolution);
    c.g = sampleCurve(lut, c.g, 2u, p.resolution);
    c.b = sampleCurve(lut, c.b, 3u, p.resolution);

    dst[tid.xy] = float4(c, 1.0f);
}
