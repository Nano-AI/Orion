// Already-linear RGB source -> white-balanced, clipped working input.
//
// The linearize stage for a file with no mosaic: a merged HDR DNG (or any
// linear DNG) arrives demosaiced, black-subtracted and normalized to 1.0 at
// its ceiling, so of linearize's four jobs only two remain — the white
// balance gains and the clip to the brightest describable neutral. The clip
// argument is linearize.slang's, restated there in full: after per-channel
// gains a blown pixel is magenta in ratio, and clipping all three channels to
// the lowest post-gain saturation level is what makes it white again.
//
// ⚠ Deliberately the same parameter block as linearize, so the host pushes
// one struct to whichever node is the graph's head and white-balance moves
// need no per-mode branch. black and invRange are identity for a linear DNG
// (0 and 1) but are applied anyway: one contract, not two.

struct LinearizeParams
{
    float4 black;        // per CFA channel, in sensor counts
    float4 whiteBalance; // per CFA channel, normalized to green
    float  invRange;     // 1 / (white - black)
    uint   filters;      // CFA pattern bitmask — unused here, layout parity
    uint2  size;
    float  whiteClip;    // common saturation level, post white balance
    float  _pad[3];
};

[shader("compute")]
[numthreads(16, 16, 1)]
void linearSource(uint3 tid : SV_DispatchThreadID,
                  uniform Texture2D<float4>    src,
                  uniform RWTexture2D<float4>  dst,
                  uniform ConstantBuffer<LinearizeParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float4 v = src[tid.xy];
    float3 rgb = (v.rgb - p.black.rgb) * p.invRange;
    rgb *= p.whiteBalance.rgb;

    dst[tid.xy] = float4(clamp(rgb, 0.0f, p.whiteClip), 1.0f);
}
