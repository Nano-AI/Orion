// Smoothness, step 1: the starlet's coarse residual at a sixteenth of the pixels.
//
// research/high-iso.md §1. Chroma noise is coarse by construction: demosaicing
// reconstructs chrominance through a low-pass (Alleysson et al. 2005, Dubois
// 2005), so the blotches are 20-60 px across and the four full-resolution
// scales stop short of them. Two more scales, j = 4 and 5 (taps 16 and 32 px),
// run here at quarter resolution.
//
// The input is c_4, the full-resolution chain's coarsest blur: by definition
// it holds only the scales the chain did not reach, so a 4x4 block average of
// it loses nothing and the quarter chain continues the same transform. The
// shrinks split luma from chroma themselves, so the block's full colour is
// kept in .rgb; .a carries its luma for anything that reads the picture.

#include "cfa.slang"
#include "ops/denoise_ops.slang"

struct ChromaDownParams
{
    uint2 size;      // quarter
    uint2 srcSize;   // full
};

[shader("compute")]
[numthreads(16, 16, 1)]
void denoiseChromaDown(uint3 tid : SV_DispatchThreadID,
                       uniform Texture2D<float4>   src,   // c_4, full resolution
                       uniform RWTexture2D<float4> dst,
                       uniform ConstantBuffer<ChromaDownParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    // The edge block is clamped, not shortened: the last pixel repeats, which
    // is the transform's own boundary rule (clampToImage).
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    const int2 o = int2(tid.xy) * 4;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            sum += src[clampToImage(o + int2(i, j), p.srcSize)].rgb;
    sum *= 1.0f / 16.0f;

    dst[tid.xy] = float4(sum, luminance2020(sum));
}
