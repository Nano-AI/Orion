// A four-channel box blur — the two-channel one, widened.
//
// Separate kernel rather than a generic one because the host binds concrete
// texture formats. The guided filter's self-guided form needs two moments and
// the cross-guided form needs four, so both exist.

struct BoxBlur4Params
{
    uint2 size;
    int   radius;
    int   horizontal;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void boxBlur4(uint3 tid : SV_DispatchThreadID,
              uniform Texture2D<float4>   src,
              uniform RWTexture2D<float4> dst,
              uniform ConstantBuffer<BoxBlur4Params> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const int2 hi = int2(p.size) - 1;
    const int2 step = (p.horizontal != 0) ? int2(1, 0) : int2(0, 1);

    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float count = 0.0f;

    for (int d = -p.radius; d <= p.radius; ++d)
    {
        const int2 q = clamp(int2(tid.xy) + step * d, int2(0, 0), hi);
        sum += src[q];
        count += 1.0f;
    }

    dst[tid.xy] = sum / max(count, 1.0f);
}
