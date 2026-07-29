// HueSatMap — the profile stage a 3x3 matrix cannot do.
//
// A camera matrix is a linear map, so it cannot correct a hue that is wrong
// only where the stimulus is saturated and spectrally narrow. Blue sky is the
// worst case on every consumer sensor: the matrix lands it toward violet.
// The DNG specification's answer is a table indexed by hue and saturation
// holding a hue shift, a saturation scale and a value scale.
//
// research/camera-profiles.md. Adobe, DNG Specification 1.6.0.0, chapter 6
// (ProfileHueSatMapDims / Data1 / Encoding).
//
// The colour space is not a detail. ProfileHueSatMapEncoding = 0 says the
// table applies in *linear ProPhoto* HSV, so the node converts in and out.
// Indexing HSV built from Rec.2020 components would put every cell at a
// different hue angle than a real profile expects — invisible against a
// hand-fitted table, wrong the day a .dcp is loaded, which is the whole
// reason for wearing the spec's shape.

struct HueSatParams
{
    float4 toProPhoto0;     // xyz = row, w unused
    float4 toProPhoto1;
    float4 toProPhoto2;
    float4 fromProPhoto0;
    float4 fromProPhoto1;
    float4 fromProPhoto2;
    uint2  size;
    uint   hueDivisions;    // table rows
    uint   satDivisions;    // table columns, >= 2
};

// Hue in [0,6) as the DNG SDK carries it, saturation and value in [0,1].
float3 rgbToHsv(float3 c)
{
    const float maxC = max(c.r, max(c.g, c.b));
    const float minC = min(c.r, min(c.g, c.b));
    const float span = maxC - minC;

    float h = 0.0f;
    if (span > 0.0f) {
        if (maxC == c.r)      h = (c.g - c.b) / span;
        else if (maxC == c.g) h = 2.0f + (c.b - c.r) / span;
        else                  h = 4.0f + (c.r - c.g) / span;
        if (h < 0.0f) h += 6.0f;
    }
    const float s = (maxC > 0.0f) ? (span / maxC) : 0.0f;
    return float3(h, s, maxC);
}

float3 hsvToRgb(float3 hsv)
{
    const float h = hsv.x, s = hsv.y, v = hsv.z;
    const int   i = int(floor(h));
    const float f = h - float(i);

    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));

    switch (i % 6) {
        case 0:  return float3(v, t, p);
        case 1:  return float3(q, v, p);
        case 2:  return float3(p, v, t);
        case 3:  return float3(p, q, v);
        case 4:  return float3(t, p, v);
        default: return float3(v, p, q);
    }
}

// Bilinear over the table: hue wraps, saturation clamps. ValueDivisions is 1,
// so the value axis is a single slice and needs no interpolation — that is the
// spec's common case and the shape a minimal correction fits in.
float3 lookup(Texture2D<float4> table, uint hueDiv, uint satDiv, float hue, float sat)
{
    const float hf = hue * (float(hueDiv) / 6.0f);
    const uint  h0 = uint(floor(hf)) % hueDiv;
    const uint  h1 = (h0 + 1u) % hueDiv;
    const float ht = hf - floor(hf);

    const float sf = clamp(sat, 0.0f, 1.0f) * float(satDiv - 1u);
    const uint  s0 = min(uint(floor(sf)), satDiv - 1u);
    const uint  s1 = min(s0 + 1u, satDiv - 1u);
    const float st = sf - floor(sf);

    const float3 a = lerp(table[uint2(s0, h0)].rgb, table[uint2(s1, h0)].rgb, st);
    const float3 b = lerp(table[uint2(s0, h1)].rgb, table[uint2(s1, h1)].rgb, st);
    return lerp(a, b, ht);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void hueSatMap(uint3 tid : SV_DispatchThreadID,
               uniform Texture2D<float4>   src,
               uniform Texture2D<float4>   table,
               uniform RWTexture2D<float4> dst,
               uniform ConstantBuffer<HueSatParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float4 source = src[tid.xy];
    const float3 c = source.rgb;

    float3 pro = float3(dot(p.toProPhoto0.rgb, c),
                        dot(p.toProPhoto1.rgb, c),
                        dot(p.toProPhoto2.rgb, c));

    // HSV is undefined on negatives, and a colour outside ProPhoto is rare
    // enough that clamping it here costs nothing a later stage can see. The
    // DNG SDK does the same before its own table lookup.
    pro = max(pro, 0.0f);

    float3 hsv = rgbToHsv(pro);
    const float3 entry = lookup(table, p.hueDivisions, p.satDivisions, hsv.x, hsv.y);

    // Entry order is the spec's: hue shift in degrees, saturation scale,
    // value scale. Sixty degrees is one hue unit.
    hsv.x = hsv.x + entry.r * (1.0f / 60.0f);
    if (hsv.x < 0.0f)      hsv.x += 6.0f;
    else if (hsv.x >= 6.0f) hsv.x -= 6.0f;
    hsv.y = clamp(hsv.y * entry.g, 0.0f, 1.0f);
    hsv.z = hsv.z * entry.b;

    const float3 out = hsvToRgb(hsv);

    dst[tid.xy] = float4(dot(p.fromProPhoto0.rgb, out),
                         dot(p.fromProPhoto1.rgb, out),
                         dot(p.fromProPhoto2.rgb, out),
                         source.a);
}
