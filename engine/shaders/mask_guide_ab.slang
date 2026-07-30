// Guided feathering, step 2 — the local linear coefficients.
//
// He, Sun & Tang (ECCV 2010 / TPAMI 2013), research/masking.md §4. The general
// form of the guided filter, of which guide_ab.slang's self-guided version is
// the special case p = I:
//
//     a = cov(I, p) / (var(I) + eps)
//     b = mean(p) - a*mean(I)
//
// Set p = I and cov(I, p) becomes var(I), which collapses to guide_ab.slang's
// a = var/(var + eps) exactly. Written as two kernels rather than one because
// the self-guided pass carries two moments and this one carries four, and a
// single kernel would have to branch on which — a branch that would be taken
// per pixel, per frame, for the life of the program, to save a fifty-line file.
//
// What the coefficient means here is worth stating, because it is not the same
// story as edge-preserving smoothing. Where the guide has an edge, var(I) is
// large, `a` is free to be large, and the output tracks the photograph — the
// mask boundary is pulled onto the edge. Where the guide is flat, `a` collapses
// toward zero and the output is just mean(p) — the mask keeps whatever shape it
// already had. So a rough boundary snaps to whatever real edge lies nearby and
// is left alone where there is nothing to snap to.

struct MaskGuideAbParams
{
    uint2 size;
    float epsilon;   // edge threshold, in squared log2-exposure units
    float _pad;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void maskGuideAb(uint3 tid : SV_DispatchThreadID,
                 uniform Texture2D<float4>   moments,  // I, I*I, p, I*p
                 uniform RWTexture2D<float2> dst,      // a, b
                 uniform ConstantBuffer<MaskGuideAbParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float4 m = moments[tid.xy];
    const float meanI  = m.x;
    const float meanII = m.y;
    const float meanP  = m.z;
    const float meanIP = m.w;

    // Clamp the variance at zero: the moments are blurred independently, so
    // rounding can leave the difference very slightly negative where the guide
    // is perfectly flat. The covariance is *not* clamped — it is signed, and
    // its sign is what lets the mask follow an edge downward as well as up.
    const float varI  = max(meanII - meanI * meanI, 0.0f);
    const float covIP = meanIP - meanI * meanP;

    const float a = covIP / (varI + p.epsilon);
    const float b = meanP - a * meanI;

    dst[tid.xy] = float2(a, b);
}
