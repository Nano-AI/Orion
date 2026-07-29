// Dehaze, step 6 — the guided filter's linear coefficients.
//
// He, Sun & Tang, "Guided Image Filtering". The cross-guided form: within a
// window, the output is a linear function of the *guide*, q = a*I + b, fitted
// to the input p by least squares —
//
//     a = cov(I, p) / (var(I) + eps)
//     b = mean(p) - a * mean(I)
//
// which reduces to the self-guided expression in guide_ab.slang when p = I.
// Both moments arrive already averaged over the window, so this is arithmetic.

struct DehazeAbParams
{
    uint2 size;
    float epsilon;
    float _pad;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void dehazeAb(uint3 tid : SV_DispatchThreadID,
              uniform Texture2D<float4>   moments,   // (g, g*g, t, g*t)
              uniform RWTexture2D<float2> dst,
              uniform ConstantBuffer<DehazeAbParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float4 m = moments[tid.xy];
    const float meanG = m.x;
    const float varG  = max(m.y - meanG * meanG, 0.0f);
    const float meanT = m.z;
    const float covGT = m.w - meanG * meanT;

    const float a = covGT / (varG + p.epsilon);
    dst[tid.xy] = float2(a, meanT - a * meanG);
}
