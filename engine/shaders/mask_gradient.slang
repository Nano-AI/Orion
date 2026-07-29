// Mask primitives — linear and radial gradients.
//
// research/masking.md §1. Both are pure functions of the pixel's normalized
// position, so there is nothing to store: a mask is its parameters, and a
// hundred masks cost a hundred structs rather than a hundred images.
//
// One kernel for both because they differ only in how they turn a position into
// a distance; the falloff, the inversion and the output are shared, and two
// shaders would mean two places to fix a feather.
//
// Output is R16Float, not R8. Alpha is a coverage value that later gets
// multiplied into parameters and, once brushes exist, accumulated across many
// low-flow dabs — eight bits bands visibly under accumulation, which is the
// same class of error as the resize that quietly cost the export its bit depth.

struct MaskParams
{
    uint2 size;
    int   kind;        // 0 none, 1 linear, 2 radial
    int   invert;

    float2 zero;       // linear: where the effect is zero
    float2 full;       // linear: where it is full

    float2 centre;     // radial
    float2 radius;     // radial semi-axes, normalized
    float  angle;      // radial rotation, radians
    float  feather;    // 0..1
    float  roundness;  // superellipse exponent; 2 is an ellipse
    float  _pad;
};

/// Perlin's smootherstep, 6t^5 - 15t^4 + 10t^3.
///
/// C² where smoothstep is only C¹, and the difference is visible: smoothstep
/// leaves a faint Mach band at the feather boundary on a clear sky, because the
/// eye finds discontinuities in the second derivative. Perlin, "Improving
/// Noise", ACM TOG 21(3), 2002, where it is introduced as the improved
/// interpolant for exactly this reason.
float maskFalloff(float t)
{
    const float x = saturate(t);
    return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void maskGradient(uint3 tid : SV_DispatchThreadID,
                  uniform RWTexture2D<float> dst,
                  uniform ConstantBuffer<MaskParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    // Pixel centres, in the normalized coordinates the mask is stored in — so a
    // mask survives a resize, and an export matches the preview it was made on.
    const float2 q = (float2(tid.xy) + 0.5f) / float2(p.size);

    float alpha = 1.0f;

    if (p.kind == 1)
    {
        // Two points carry position and angle together, so there is no separate
        // angle field to keep consistent with them.
        const float2 d = p.full - p.zero;
        const float denom = dot(d, d);
        const float t = (denom > 1e-9f) ? dot(q - p.zero, d) / denom : 1.0f;
        alpha = maskFalloff(t);
    }
    else if (p.kind == 2)
    {
        const float c = cos(p.angle), s = sin(p.angle);
        const float2 e = q - p.centre;
        const float u = ( c * e.x + s * e.y) / max(p.radius.x, 1e-6f);
        const float v = (-s * e.x + c * e.y) / max(p.radius.y, 1e-6f);

        // n = 2 is an ellipse; raising it walks toward a rounded rectangle.
        const float n = max(p.roundness, 0.1f);
        const float r = pow(pow(abs(u), n) + pow(abs(v), n), 1.0f / n);

        // r = 1 is the boundary. Feather is how far inside it the ramp starts.
        const float f = clamp(p.feather, 0.0f, 0.999f);
        alpha = 1.0f - maskFalloff((r - (1.0f - f)) / max(f, 1e-3f));
    }

    dst[tid.xy] = (p.invert != 0) ? 1.0f - alpha : alpha;
}
