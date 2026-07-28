// Sensor samples -> normalised linear CFA.
//
// Subtracts the per-channel black point, scales to 0..1 against the saturation
// point, and applies the as-shot white balance. Output is still a mosaic — one
// channel per pixel — but now scene-linear float.

#include "cfa.slang"

struct LinearizeParams
{
    float4 black;        // per CFA channel, in sensor counts
    float4 whiteBalance; // per CFA channel, normalised to green
    float  invRange;     // 1 / (white - black)
    uint   filters;      // CFA pattern bitmask
    uint2  size;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void linearize(uint3 tid : SV_DispatchThreadID,
               uniform Texture2D<uint>      src,
               uniform RWTexture2D<float>   dst,
               uniform ConstantBuffer<LinearizeParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const uint  c = cfaChannel(tid.xy, p.filters);
    const float raw = float(src[tid.xy].x);

    float v = (raw - p.black[c]) * p.invRange;
    v *= p.whiteBalance[c];

    dst[tid.xy] = max(v, 0.0f);
}
