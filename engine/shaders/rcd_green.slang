// RCD pass 2 — the green plane.
//
// Green carries most of the luminance detail, so its quality sets the ceiling
// for the whole demosaic. At green sites we pass the sample through untouched.
// At red and blue sites we build vertical and horizontal estimates, correct
// each with the local second derivative of the centre channel, then blend by
// the direction weight from pass 1.
//
// The clamp at the end is the ratio-correction step's job: it stops the
// gradient correction from pushing the estimate outside the bracketing green
// samples, which is exactly where Hamilton–Adams alone generates maze artefacts.

#include "cfa.slang"

struct GreenParams
{
    uint2 size;
    uint  filters;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void rcdGreen(uint3 tid : SV_DispatchThreadID,
              uniform Texture2D<float>   cfa,
              uniform Texture2D<float>   dirs,
              uniform RWTexture2D<float> green,
              uniform ConstantBuffer<GreenParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    if (isGreen(tid.xy, p.filters))
    {
        green[tid.xy] = cfa[tid.xy];
        return;
    }

    const int2  c  = int2(tid.xy);
    const uint2 sz = p.size;

    const float centre = tapClamped(cfa, c, sz);

    const float gN = tapClamped(cfa, c + int2(0, -1), sz);
    const float gS = tapClamped(cfa, c + int2(0,  1), sz);
    const float gW = tapClamped(cfa, c + int2(-1, 0), sz);
    const float gE = tapClamped(cfa, c + int2( 1, 0), sz);
    const float cN = tapClamped(cfa, c + int2(0, -2), sz);
    const float cS = tapClamped(cfa, c + int2(0,  2), sz);
    const float cW = tapClamped(cfa, c + int2(-2, 0), sz);
    const float cE = tapClamped(cfa, c + int2( 2, 0), sz);

    // Directional estimates, each with the centre-channel curvature folded in.
    float estV = 0.5f * (gN + gS) + 0.25f * (2.0f * centre - cN - cS);
    float estH = 0.5f * (gW + gE) + 0.25f * (2.0f * centre - cW - cE);

    // Ratio correction: keep each estimate inside its own bracketing pair.
    estV = clamp(estV, min(gN, gS), max(gN, gS));
    estH = clamp(estH, min(gW, gE), max(gW, gE));

    green[tid.xy] = lerp(estV, estH, dirs[tid.xy]);
}
