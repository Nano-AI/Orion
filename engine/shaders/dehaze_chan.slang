// Dehaze, step 1 — the per-pixel channel minimum.
//
// He, Sun & Tang, "Single Image Haze Removal Using Dark Channel Prior",
// CVPR 2009 / TPAMI 33(12), 2011. research/dehaze.md.
//
// Eq. (5)'s inner term, and Eq. (11)'s, are the same expression with different
// scaling: min over channels of I_c, or of I_c/A_c. One kernel serves both —
// pass A = (1,1,1) and it is the plain dark channel used to *find* A; pass the
// real A and it is the normalized image the transmission is estimated from.
//
// The clamp to 1 is Orion's, not the paper's, and it is load-bearing here.
// Their inputs are gamma-encoded photographs bounded at 1; this pipeline is
// scene-linear and unbounded, so a specular reflection can read I_c > A_c,
// which would drive Eq. (12)'s transmission negative and Eq. (16) would then
// divide by a negative number and invert the pixel.

struct DehazeChanParams
{
    uint2 size;
    float2 _pad;
    float4 airlight;   // A_c, w unused
};

[shader("compute")]
[numthreads(16, 16, 1)]
void dehazeChannelMin(uint3 tid : SV_DispatchThreadID,
                      uniform Texture2D<float4>  src,
                      uniform RWTexture2D<float> dst,
                      uniform ConstantBuffer<DehazeChanParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float3 a = max(p.airlight.rgb, float3(1e-6f, 1e-6f, 1e-6f));
    const float3 n = src[tid.xy].rgb / a;

    dst[tid.xy] = clamp(min(min(n.r, n.g), n.b), 0.0f, 1.0f);
}
