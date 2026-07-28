// Exposure, in EV, on scene-linear data.
//
// A pure multiply — which is the entire argument for a scene-referred
// pipeline. On display-referred data the same slider needs a curve, behaves
// differently in shadows than highlights, and shifts hue as it goes.

struct ExposureParams
{
    float ev;
    float black;   // lift/crush, applied after the gain
    uint2 size;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void exposure(uint3 tid : SV_DispatchThreadID,
              uniform Texture2D<float4>   src,
              uniform RWTexture2D<float4> dst,
              uniform ConstantBuffer<ExposureParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float gain = exp2(p.ev);
    const float3 c = src[tid.xy].rgb * gain + p.black;

    dst[tid.xy] = float4(c, 1.0f);
}
