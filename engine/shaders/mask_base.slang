// The identity a mask group's fold starts from.
//
// research/masking.md §6 folds components left in listed order. A fold needs a
// starting value, and this node writes it so that every component node has the
// same shape: one alpha input, one alpha output. Without it the first component
// would be a different kind of node from the rest — no input to read, a
// "replace" op nothing else uses — and a group of one would exercise a code path
// a group of two does not.
//
// **Zero is the additive identity**, so the first component's `add` reduces to
// `max(0, own)` = `own` with no special case anywhere. The value is a parameter
// rather than a literal because a group that opened on `intersect` would want to
// start at one; nothing asks for that yet, and when something does this node is
// where it goes.
//
// Cheap and effectively free after the first render: the params never change, so
// the per-node cache serves it for the life of the image.

struct MaskBaseParams
{
    uint2 size;
    float value;
    float _pad;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void maskBase(uint3 tid : SV_DispatchThreadID,
              uniform RWTexture2D<float> dst,
              uniform ConstantBuffer<MaskBaseParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;
    dst[tid.xy] = p.value;
}
