// Exposure fusion, step 2 — the simulated images, and their normalized weights.
//
// One kernel, two modes, because the two outputs are point-wise functions of the
// same input and differ only in what they emit. Four of the eight slots per
// dispatch, so a five-image plan costs two dispatches per output.
//
// The weights are normalized here rather than later: the normalisation is a sum
// over *all* the simulated images, so it has to happen where all of them are in
// hand, and after this point each stack can be blurred independently.

#include "ops/fuse_ops.slang"

struct FuseSplitParams
{
    uint2 size;
    int   base;        // index of this texture's first slot
    int   weights;     // nonzero to emit weights instead of images
    FusePlan plan;
    float epsilon;     // guards the weight normalisation; no paper specifies one
    float _pad[3];
};

[shader("compute")]
[numthreads(16, 16, 1)]
void fuseSplit(uint3 tid : SV_DispatchThreadID,
               uniform Texture2D<float>    proxy,
               uniform RWTexture2D<float4> dst,
               uniform ConstantBuffer<FuseSplitParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float t = saturate(proxy[tid.xy]);

    float total = 0.0f;
    if (p.weights != 0)
    {
        for (int k = -p.plan.dark; k <= p.plan.bright; ++k)
        {
            total += fuseWeight(t, k, p.plan);
        }
        total = max(total, p.epsilon);
    }

    float4 out = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int slot = 0; slot < 4; ++slot)
    {
        const int index = p.base + slot;          // 0 .. M-1
        if (index >= p.plan.images) continue;
        const int k = index - p.plan.dark;        // -N* .. N

        out[slot] = (p.weights != 0) ? fuseWeight(t, k, p.plan) / total
                                     : fuseRemap(t, k, p.plan);
    }

    dst[tid.xy] = out;
}
