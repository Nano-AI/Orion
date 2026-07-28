// Guided filter, step 2 — the local linear coefficients.
//
// He, Sun & Tang's key result: within any window, the filtered output is a
// linear function of the guide, q = a·I + b, with
//
//     a = var(I) / (var(I) + eps)
//     b = mean(I) − a·mean(I)
//
// eps is what sets the edge threshold. Where local variance is large compared
// with eps — an edge — a tends to 1 and the filter passes the signal through
// untouched. Where variance is small — flat regions, and noise — a tends to 0
// and the output collapses to the local mean. That single ratio is why the
// filter smooths without bleeding across edges, and it is what stops highlight
// recovery producing halos around a skyline.
//
// Averaging a and b over windows afterwards (the box blurs that follow) is what
// makes the result continuous rather than blocky.

struct GuideAbParams
{
    uint2 size;
    float epsilon;   // edge threshold, in squared log2-exposure units
    float _pad;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void guideAb(uint3 tid : SV_DispatchThreadID,
             uniform Texture2D<float2>   moments,   // mean(I), mean(I*I)
             uniform RWTexture2D<float2> dst,       // a, b
             uniform ConstantBuffer<GuideAbParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float2 m = moments[tid.xy];
    const float meanI = m.x;
    const float meanII = m.y;

    // Clamp at zero: the two moments are blurred independently, so rounding
    // can leave the difference very slightly negative in perfectly flat areas.
    const float varI = max(meanII - meanI * meanI, 0.0f);

    const float a = varI / (varI + p.epsilon);
    const float b = meanI - a * meanI;

    dst[tid.xy] = float2(a, b);
}
