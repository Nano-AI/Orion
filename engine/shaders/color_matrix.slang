// Camera RGB -> linear Rec.2020, the pipeline's working space.
//
// Everything downstream of here is scene-referred linear in a single wide
// space, exactly once. See planning/RESEARCH.md §2 for why that ordering is
// non-negotiable.

struct MatrixParams
{
    float4 row0;   // xyz = camera->working row, w unused
    float4 row1;
    float4 row2;
    uint2  size;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void cameraToWorking(uint3 tid : SV_DispatchThreadID,
                     uniform Texture2D<float4>   src,
                     uniform RWTexture2D<float4> dst,
                     uniform ConstantBuffer<MatrixParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float3 c = src[tid.xy].rgb;

    const float3 working = float3(dot(p.row0.rgb, c),
                                  dot(p.row1.rgb, c),
                                  dot(p.row2.rgb, c));

    // Negative values are legitimate here — they are colours outside the
    // working gamut, and clipping them now would bake in a hue shift the tone
    // mapper could otherwise resolve gracefully.
    dst[tid.xy] = float4(working, 1.0f);
}
