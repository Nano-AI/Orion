// RCD pass 1 — directional discrimination.
//
// Decides, per pixel, whether the local signal is smoother vertically or
// horizontally. Interpolating along the smooth direction is what keeps edges
// clean; guessing wrong is what produces zippers and maze artefacts.
//
// Output is a single weight in 0..1: 0 = interpolate vertically,
// 1 = interpolate horizontally.

#include "cfa.slang"

struct DirParams
{
    uint2 size;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void rcdDirs(uint3 tid : SV_DispatchThreadID,
             uniform Texture2D<float>    cfa,
             uniform RWTexture2D<float>  dirs,
             uniform ConstantBuffer<DirParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const int2  c   = int2(tid.xy);
    const uint2 sz  = p.size;
    const float eps = 1e-6f;

    const float centre = tapClamped(cfa, c, sz);

    const float n1 = tapClamped(cfa, c + int2(0, -1), sz);
    const float s1 = tapClamped(cfa, c + int2(0,  1), sz);
    const float n2 = tapClamped(cfa, c + int2(0, -2), sz);
    const float s2 = tapClamped(cfa, c + int2(0,  2), sz);
    const float n3 = tapClamped(cfa, c + int2(0, -3), sz);
    const float s3 = tapClamped(cfa, c + int2(0,  3), sz);

    const float w1 = tapClamped(cfa, c + int2(-1, 0), sz);
    const float e1 = tapClamped(cfa, c + int2( 1, 0), sz);
    const float w2 = tapClamped(cfa, c + int2(-2, 0), sz);
    const float e2 = tapClamped(cfa, c + int2( 2, 0), sz);
    const float w3 = tapClamped(cfa, c + int2(-3, 0), sz);
    const float e3 = tapClamped(cfa, c + int2( 3, 0), sz);

    // Gradient energy along each axis, sampled at both the immediate
    // neighbours (opposite CFA channel) and two out (same channel).
    const float vertical =
          abs(n1 - s1)
        + abs(2.0f * centre - n2 - s2)
        + 0.5f * (abs(n1 - n3) + abs(s1 - s3));

    const float horizontal =
          abs(w1 - e1)
        + abs(2.0f * centre - w2 - e2)
        + 0.5f * (abs(w1 - w3) + abs(e1 - e3));

    // Squaring sharpens the decision on strong edges while leaving flat areas
    // near 0.5, where the two estimates agree anyway and the blend is harmless.
    const float v2 = vertical * vertical;
    const float h2 = horizontal * horizontal;

    dirs[tid.xy] = v2 / (v2 + h2 + eps);
}
