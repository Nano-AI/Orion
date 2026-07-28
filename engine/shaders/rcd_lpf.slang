// RCD step 2 — the low-pass filter.
//
// Reference: Luis Sanz Rodríguez, https://github.com/LuisSR/RCD-Demosaicing (MIT).
//
//     lpf = 0.25·c + 0.125·(4-neighbours) + 0.0625·(diagonals)
//
// A 3x3 binomial kernel over the mosaic, mixing all three colours. That is the
// point: it estimates local *luminance* regardless of which CFA channel each
// sample happens to carry, and step 3 uses ratios of it to correct the green
// estimate. Without this, there is no ratio correction and the algorithm
// degenerates to Hamilton–Adams.

#include "cfa.slang"

struct LpfParams
{
    uint2 size;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void rcdLpf(uint3 tid : SV_DispatchThreadID,
            uniform Texture2D<float>   cfa,
            uniform RWTexture2D<float> lpf,
            uniform ConstantBuffer<LpfParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const int2  c  = int2(tid.xy);
    const uint2 sz = p.size;

    const float centre = tapClamped(cfa, c, sz);

    const float cross =
          tapClamped(cfa, c + int2( 0, -1), sz)
        + tapClamped(cfa, c + int2( 0,  1), sz)
        + tapClamped(cfa, c + int2(-1,  0), sz)
        + tapClamped(cfa, c + int2( 1,  0), sz);

    const float diag =
          tapClamped(cfa, c + int2(-1, -1), sz)
        + tapClamped(cfa, c + int2( 1, -1), sz)
        + tapClamped(cfa, c + int2(-1,  1), sz)
        + tapClamped(cfa, c + int2( 1,  1), sz);

    lpf[tid.xy] = 0.25f * centre + 0.125f * cross + 0.0625f * diag;
}
