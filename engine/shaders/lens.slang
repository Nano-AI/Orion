// Lens corrections — distortion, transverse chromatic aberration, vignetting.
//
// Reference: lensfun's model definitions, as set out in
// research/deep-research-2026-07-27.md §4. Radii are normalized against
// R_norm = ½·sqrt(width² + height²), which is lensfun's convention and the one
// every published coefficient assumes.
//
//     poly3:  r_d = r_u · ( (1 − k₁) + k₁·r_u² )
//     TCA:    r_R = r_G · ( v_R + k_R·r_G² ),  green is the reference
//     vig:    V(r) = 1 + p_a·r² + p_b·r⁴ + p_c·r⁶,  I_corrected = I_raw / V(r)
//
// The three are one sampling pass because they are one coordinate transform per
// channel. Doing them separately would resample three times and soften three
// times over.
//
// Position: after denoise, before sharpening. Resampling softens, and
// sharpening is the thing that answers for that — in the other order it would
// sharpen detail the resample is about to smear.
//
// Note that the geometry node at the end of the graph resamples again, for
// crop and straighten. Two resamples is one more than ideal; composing them is
// not possible while one runs on linear light and the other on display-encoded
// pixels, and moving either would cost more than the softening does.

struct LensParams
{
    uint2 size;
    float centerX, centerY;   // optical center, normalized
    float k1;                 // poly3 distortion
    float caRed, caBlue;      // TCA, as a fractional radial scale
    float vignetteA;          // p_a; p_b and p_c stay zero for a manual control
    float _pad[4];
};

/// Bilinear fetch. Outside the frame returns the edge, not transparency: a lens
/// correction pulls pixels in from just past the border, and a hole there would
/// read as a defect rather than as honesty about missing data.
float3 sampleClamped(Texture2D<float4> src, float2 p, uint2 size)
{
    const float2 maxCoord = float2(size) - 1.0f;
    const float2 q = clamp(p, float2(0.0f, 0.0f), maxCoord);

    const int2 i0 = int2(floor(q));
    const int2 i1 = min(i0 + int2(1, 1), int2(maxCoord));
    const float2 f = q - float2(i0);

    const float3 a = lerp(src[int2(i0.x, i0.y)].rgb, src[int2(i1.x, i0.y)].rgb, f.x);
    const float3 b = lerp(src[int2(i0.x, i1.y)].rgb, src[int2(i1.x, i1.y)].rgb, f.x);
    return lerp(a, b, f.y);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void lensCorrect(uint3 tid : SV_DispatchThreadID,
                 uniform Texture2D<float4>   src,
                 uniform RWTexture2D<float4> dst,
                 uniform ConstantBuffer<LensParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float2 size = float2(p.size);
    const float2 center = float2(p.centerX, p.centerY) * size;

    // lensfun's normalizing radius: half the diagonal, so r = 1 at the corners.
    const float rNorm = 0.5f * length(size);

    const float2 d = (float2(tid.xy) + 0.5f) - center;
    const float ru = length(d) / rNorm;

    // poly3. The (1 − k₁) term is what pins r_d(1) = 1, so the frame corners
    // stay put and only the interior moves — without it the whole picture
    // scales as the slider moves, which reads as a zoom rather than a
    // correction.
    const float distort = (1.0f - p.k1) + p.k1 * ru * ru;

    // Green is the reference for transverse chromatic aberration; red and blue
    // are rescaled radially about it. The correction is the inverse of the
    // aberration, hence the sign.
    const float3 scale = float3(distort * (1.0f - p.caRed * ru * ru),
                                distort,
                                distort * (1.0f - p.caBlue * ru * ru));

    float3 c;
    c.r = sampleClamped(src, center + d * scale.r, p.size).r;
    c.g = sampleClamped(src, center + d * scale.g, p.size).g;
    c.b = sampleClamped(src, center + d * scale.b, p.size).b;

    // Vignetting is measured at the *corrected* radius, because that is where
    // the light actually fell on the sensor.
    if (abs(p.vignetteA) > 1e-6f)
    {
        const float rc = ru * distort;
        const float r2 = rc * rc;
        const float v = 1.0f + p.vignetteA * r2;
        c /= max(v, 0.05f);
    }

    dst[tid.xy] = float4(max(c, 0.0f), 1.0f);
}
