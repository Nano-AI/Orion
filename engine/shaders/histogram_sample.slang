// The histogram's samples (#DEC-perf): every `stride`-th pixel of the picture,
// in reading order, packed into a small target the CPU reads back, so the
// histogram reads 1/stride of the frame instead of all of it. The same pixels
// Engine::histogram sampled from a full readback, in the source's own format:
// no value changes on the way, so the bins are what they were.
//
// Not a filter: a strided gather.

struct HistogramSampleParams
{
    uint2 size;     // the target's size
    uint  width;    // the picture's width in the source (its top-left region)
    uint  count;    // pixels in the picture
    uint  stride;   // source pixels per sample
    uint  unused;   // the struct is 24 bytes either way (uint2's alignment); said, not implied
};

[shader("compute")]
[numthreads(16, 16, 1)]
void histogramSample(uint3 tid : SV_DispatchThreadID,
                     uniform Texture2D<float4>   src,
                     uniform RWTexture2D<float4> dst,
                     uniform ConstantBuffer<HistogramSampleParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const uint i = (tid.y * p.size.x + tid.x) * p.stride;
    if (i >= p.count) return;   // past the last sample: never read back
    dst[tid.xy] = src[uint2(i % p.width, i / p.width)];
}
