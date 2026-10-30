// Lens corrections — distortion, transverse chromatic aberration, vignetting.
//
// One general warp that three published sources all fit (research/
// lens-corrections.md):
//
//     lensfun ptlens/poly3   f = (1 − a − b − c) + c·r + b·r² + a·r³
//     DNG WarpRectilinear    f = kr0 + kr1·r² + kr2·r⁴ + kr3·r⁶, plus tangential
//                            (Adobe DNG Specification 1.6, chapter 7, opcode 1)
//     Adobe Camera Model     f = 1 + k1·r² + k2·r⁴ + k3·r⁶, plus tangential
//
//     source = c + f·(q − c) + norm·t,  d = (q − c) / norm
//     vig:   V(r) = 1 + v1·r² + … + v5·r¹⁰, divided out (lensfun, Adobe) or
//            multiplied in (DNG FixVignetteRadial, opcode 3)
//
// Each source normalizes r its own way; pipe/LensGeometry.h `resolve` turns
// that into `center` and `norm` in pixels, and is the only place that knows.
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
    float2 center;            // optical center, pixels
    float2 norm;              // pixels per unit of normalized radius
    float caRed, caBlue;      // manual TCA, a fractional radial scale at rDiag
    float4 radialLo[3];       // per channel: k0, k1, k2, k3
    float4 radialHi[3];       // per channel: k4, k6, tangential t0, t1
    float4 vig;               // vignette terms in r², r⁴, r⁶, r⁸
    float vig10;              // and r¹⁰
    uint vigFlags;            // bit 0 multiply (gain) else divide; bit 1 at the output position
    float2 vigCenter;
    float2 vigNorm;
    float scale;              // autoscale; see pipe/LensGeometry.h
    float rDiag;              // half the diagonal, for the manual TCA
};

/// Bilinear fetch, in texel coordinates. Outside the frame returns the edge
/// rather than transparency — but that is a backstop, not the plan: `scale`
/// exists so nothing ever asks for a pixel that is not there. When it did, the
/// clamp turned into a band of one column smeared sideways.
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

/// Output position (pixels, already zoomed) to the source position it reads,
/// for one channel. pipe/LensGeometry.h `warpSource` is the host copy, line
/// for line — the autoscale and the inverse map are only as right as that.
float2 warpSource(LensParams p, uint ch, float2 q)
{
    const float2 e = q - p.center;
    const float2 d = e / p.norm;
    const float r2 = dot(d, d);
    const float r = sqrt(r2);
    const float4 lo = p.radialLo[ch];
    const float4 hi = p.radialHi[ch];
    const float f = lo.x + r * (lo.y + r * (lo.z + r * lo.w)) + r2 * r2 * (hi.x + r2 * hi.y);
    const float2 t = float2(hi.z * 2.0f * d.x * d.y + hi.w * (r2 + 2.0f * d.x * d.x),
                            hi.w * 2.0f * d.x * d.y + hi.z * (r2 + 2.0f * d.y * d.y));
    // f·e rather than f·d·norm: a divide undone by a multiply is not exact in
    // float, and an identity that misses by a rounding error drops a whole
    // fp16 step wherever a fetch lands a hair short of a texel.
    return p.center + f * e + t * p.norm;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void lensCorrect(uint3 tid : SV_DispatchThreadID,
                 uniform Texture2D<float4>   src,
                 uniform RWTexture2D<float4> dst,
                 uniform ConstantBuffer<LensParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    // Autoscale: the zoom that brings the outermost fetch back to the frame
    // edge, so no output pixel reads past the border. A warp that reaches
    // outside otherwise turns the clamp into a band of one column smeared
    // sideways. The host works it out; see pipe/LensGeometry.h.
    const float zoom = p.scale > 0.0f ? p.scale : 1.0f;
    const float2 q = p.center + ((float2(tid.xy) + 0.5f) - p.center) * zoom;

    const float2 sg = warpSource(p, 1, q);
    float2 sr = warpSource(p, 0, q);
    float2 sb = warpSource(p, 2, q);

    // Manual transverse chromatic aberration: red and blue rescaled about the
    // optical center against green, the reference. Measured at lensfun's
    // radius so a slider position means the same whatever the profile source.
    // The correction is the inverse of the aberration, hence the sign.
    const float2 e = q - p.center;
    const float ru2 = dot(e, e) / (p.rDiag * p.rDiag);
    sr = p.center + (sr - p.center) * (1.0f - p.caRed * ru2);
    sb = p.center + (sb - p.center) * (1.0f - p.caBlue * ru2);

    // Minus half a texel, because positions are measured from pixel *centers*
    // and sampleClamped indexes texels. Without it every fetch landed exactly
    // between two texels — a half-pixel shift and a bilinear blur applied to
    // the whole frame the moment any lens slider left zero.
    float3 c;
    c.r = sampleClamped(src, sr - 0.5f, p.size).r;
    c.g = sampleClamped(src, sg - 0.5f, p.size).g;
    c.b = sampleClamped(src, sb - 0.5f, p.size).b;

    // Vignetting. lensfun and Adobe measure it where the light fell — the
    // source position — and publish a falloff to divide out. A DNG
    // FixVignetteRadial publishes a gain to multiply, and lands on the output
    // position when it follows the warp in the opcode list.
    if (any(p.vig != 0.0f) || p.vig10 != 0.0f)
    {
        const float2 at = (p.vigFlags & 2u) != 0u ? q : sg;
        const float2 v = (at - p.vigCenter) / p.vigNorm;
        const float r2 = dot(v, v);
        const float g = 1.0f + r2 * (p.vig.x + r2 * (p.vig.y + r2 * (p.vig.z
                      + r2 * (p.vig.w + r2 * p.vig10))));
        c = (p.vigFlags & 1u) != 0u ? c * g : c / max(g, 0.05f);
    }

    dst[tid.xy] = float4(c, 1.0f);
}
