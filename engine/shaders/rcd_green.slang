// RCD step 3 — green interpolation with ratio correction.
//
// Reference: Luis Sanz Rodríguez, https://github.com/LuisSR/RCD-Demosaicing (MIT).
//
// This is the step that gives the algorithm its name and the one that was
// missing before. Rather than adding a gradient correction to the neighbouring
// green (Hamilton–Adams), each cardinal estimate is *scaled* by a ratio of the
// local low-pass values:
//
//     N_Est = cfa[N] · (1 + (lpf[c] − lpf[N2]) / (eps + lpf[c] + lpf[N2]))
//
// A ratio tracks a luminance step correctly where an additive correction
// overshoots it, which is why RCD holds fine texture that Hamilton–Adams
// smears and does not need an artefact-suppressing clamp to stay clean.
//
// The four estimates are then combined by *opposing* gradient weights: an
// estimate from a direction with a small gradient is trusted more, so the
// weight applied to N_Est is S_Grad, not N_Grad.

#include "cfa.slang"
#include "ops/rcd_stat.slang"

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
              uniform Texture2D<float>   lpf,
              uniform RWTexture2D<float> green,
              uniform ConstantBuffer<GreenParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    // Green sites already carry green.
    if (isGreen(tid.xy, p.filters))
    {
        green[tid.xy] = cfa[tid.xy];
        return;
    }

    const int2  c  = int2(tid.xy);
    const uint2 sz = p.size;

    const float c0 = tapClamped(cfa, c, sz);

    const float n1 = tapClamped(cfa, c + int2(0, -1), sz);
    const float s1 = tapClamped(cfa, c + int2(0,  1), sz);
    const float w1 = tapClamped(cfa, c + int2(-1, 0), sz);
    const float e1 = tapClamped(cfa, c + int2( 1, 0), sz);

    const float n2 = tapClamped(cfa, c + int2(0, -2), sz);
    const float s2 = tapClamped(cfa, c + int2(0,  2), sz);
    const float w2 = tapClamped(cfa, c + int2(-2, 0), sz);
    const float e2 = tapClamped(cfa, c + int2( 2, 0), sz);

    const float n3 = tapClamped(cfa, c + int2(0, -3), sz);
    const float s3 = tapClamped(cfa, c + int2(0,  3), sz);
    const float w3 = tapClamped(cfa, c + int2(-3, 0), sz);
    const float e3 = tapClamped(cfa, c + int2( 3, 0), sz);

    const float n4 = tapClamped(cfa, c + int2(0, -4), sz);
    const float s4 = tapClamped(cfa, c + int2(0,  4), sz);
    const float w4 = tapClamped(cfa, c + int2(-4, 0), sz);
    const float e4 = tapClamped(cfa, c + int2( 4, 0), sz);

    // Cardinal gradients: how much the signal moves going that way.
    const float nGrad = kRcdEps + abs(n1 - s1) + abs(c0 - n2) + abs(n1 - n3) + abs(n2 - n4);
    const float sGrad = kRcdEps + abs(s1 - n1) + abs(c0 - s2) + abs(s1 - s3) + abs(s2 - s4);
    const float wGrad = kRcdEps + abs(w1 - e1) + abs(c0 - w2) + abs(w1 - w3) + abs(w2 - w4);
    const float eGrad = kRcdEps + abs(e1 - w1) + abs(c0 - e2) + abs(e1 - e3) + abs(e2 - e4);

    // Ratio-corrected estimates. The lpf ratio is the correction.
    const float lc = lpf[tid.xy];
    const float ln = tapClamped(lpf, c + int2(0, -2), sz);
    const float ls = tapClamped(lpf, c + int2(0,  2), sz);
    const float lw = tapClamped(lpf, c + int2(-2, 0), sz);
    const float le = tapClamped(lpf, c + int2( 2, 0), sz);

    const float nEst = n1 * (1.0f + (lc - ln) / (kRcdEps + lc + ln));
    const float sEst = s1 * (1.0f + (lc - ls) / (kRcdEps + lc + ls));
    const float wEst = w1 * (1.0f + (lc - lw) / (kRcdEps + lc + lw));
    const float eEst = e1 * (1.0f + (lc - le) / (kRcdEps + lc + le));

    // Opposing weights: trust the estimate from the calmer direction.
    const float vEst = (sGrad * nEst + nGrad * sEst) / (nGrad + sGrad);
    const float hEst = (wGrad * eEst + eGrad * wEst) / (eGrad + wGrad);

    const float vhDisc = dirs[tid.xy];
    green[tid.xy] = clamp(vhDisc * hEst + (1.0f - vhDisc) * vEst, 0.0f, 1.0f);
}
