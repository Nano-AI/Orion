// Dehaze, step 4 — the candidates for the atmospheric light.
//
// CVPR section 4.4: "We first pick the top 0.1 percent brightest pixels in the
// dark channel... Among these pixels, the pixels with highest intensity in the
// input image I are selected as the atmospheric light." Two stages, and the
// paper is explicit that the answer "may not be brightest ones in the whole
// input image" — picking the brightest pixel outright would hand A to a
// specular highlight and solve the entire frame against the wrong constant.
//
// A reduction over the frame is the one part of this method that is not a
// per-pixel operation, so this kernel does the pooling and the host finishes
// the job on a texture a sixteenth the size. Max pooling, not averaging: the
// step is looking for the most haze-opaque extremes, and an average of a block
// is not in the block.
//
// Each output carries the winning pixel's own color alongside its dark value,
// so the host never has to go back to the full-resolution image to find it.

struct DehazePeakParams
{
    uint2 outSize;
    uint2 inSize;
    int   scale;
    int   _pad[3];
};

[shader("compute")]
[numthreads(16, 16, 1)]
void dehazePeak(uint3 tid : SV_DispatchThreadID,
                uniform Texture2D<float>    dark,
                uniform Texture2D<float4>   image,
                uniform RWTexture2D<float4> dst,
                uniform ConstantBuffer<DehazePeakParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    const int s = max(p.scale, 1);
    const int2 origin = int2(tid.xy) * s;

    float best = -1.0f;
    float3 color = float3(0.0f, 0.0f, 0.0f);

    for (int dy = 0; dy < s; ++dy)
    {
        const int y = origin.y + dy;
        if (y >= int(p.inSize.y)) continue;
        for (int dx = 0; dx < s; ++dx)
        {
            const int x = origin.x + dx;
            if (x >= int(p.inSize.x)) continue;

            const float d = dark[int2(x, y)];
            if (d > best)
            {
                best = d;
                color = image[int2(x, y)].rgb;
            }
        }
    }

    dst[tid.xy] = float4(max(best, 0.0f), color);
}
