// Smoothness, step 3: the coarse chroma correction, back at full resolution.
//
// research/high-iso.md §1. What is expanded is `shrunk − unshrunk`, the
// correction the two quarter-resolution shrinks made, not the picture: the
// full-resolution luma and fine chroma pass to the bit, and only a field that
// is smooth by construction is resampled. Bilinear, on the block centres the
// down pass averaged around.

#include "cfa.slang"
#include "ops/denoise_ops.slang"

struct ChromaUpParams
{
    uint2 size;          // full
    uint2 quarterSize;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void denoiseChromaUp(uint3 tid : SV_DispatchThreadID,
                     uniform Texture2D<float4>   picture,   // denoise:shrink 0
                     uniform Texture2D<float4>   shrunk,    // quarter, after j = 4, 5
                     uniform Texture2D<float4>   unshrunk,  // quarter, the block means
                     uniform RWTexture2D<float4> dst,
                     uniform ConstantBuffer<ChromaUpParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    // Block (bx, by) covers full pixels 4bx..4bx+3, so its centre is 4bx + 1.5.
    const float2 u = (float2(tid.xy) - 1.5f) / 4.0f;
    const int2 b0 = int2(floor(u));
    const float2 f = u - float2(b0);

    float3 delta = float3(0.0f, 0.0f, 0.0f);
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i)
        {
            const int2 q = clampToImage(b0 + int2(i, j), p.quarterSize);
            const float w = (i == 0 ? 1.0f - f.x : f.x) * (j == 0 ? 1.0f - f.y : f.y);
            delta += w * (shrunk[q].rgb - unshrunk[q].rgb);
        }

    // The shrinks keep their chroma on the zero-luminance plane already; the
    // re-projection is so that a resampled sum cannot leak into luma.
    delta -= luminance2020(delta);

    const float4 c = picture[tid.xy];
    dst[tid.xy] = float4(c.rgb + delta, c.a);
}
