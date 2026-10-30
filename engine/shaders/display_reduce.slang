// The canvas's copy of a render (#386): the picture `block` pixels to a side
// averaged into one, written to an RGBA8 target the CPU reads back.
//
// block 1 is a plain conversion (a wide RGBA16F output to 8 bits on the GPU,
// not in a CPU loop). block 2^k is present.mm's chain for the shared surface,
// k 2x2 halvings, in one pass: a power-of-two box is what k halvings average,
// without k - 1 intermediate textures or a rounding to 8 bits between them.
// Reads clamp to the picture's own region, not the square texture's, so an odd
// edge averages no dead texel (present.mm's orionHalve does the same).
//
// Not a published filter: a box average, the simplest reduction there is.

struct DisplayReduceParams
{
    uint2 size;    // the target's size
    uint2 valid;   // the picture in the source, its top-left region
    uint2 block;   // source pixels per target pixel, each way
};

[shader("compute")]
[numthreads(16, 16, 1)]
void displayReduce(uint3 tid : SV_DispatchThreadID,
                   uniform Texture2D<float4>   src,
                   uniform RWTexture2D<float4> dst,
                   uniform ConstantBuffer<DisplayReduceParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const uint2 last = p.valid - 1;
    const uint2 base = tid.xy * p.block;
    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (uint y = 0; y < p.block.y; ++y)
    {
        for (uint x = 0; x < p.block.x; ++x)
        {
            sum += src[min(base + uint2(x, y), last)];
        }
    }
    dst[tid.xy] = saturate(sum / float(p.block.x * p.block.y));
}
