// Local Laplacian, last step — put the filtered luminance back on the picture.
//
// Paris et al. §5.3: an intensity image and color ratios rho = I / I_i, filter
// the intensity, multiply the ratios back. Scaling all three channels by one
// factor is exactly that, and it is what keeps the hue and saturation of every
// pixel where the camera profile put them — Figure 9 in the paper is the same
// frame filtered both ways, and the per-channel version visibly moves color.
//
// The window normalization cancels here: only the *difference* between the
// filtered and original log luminance survives into the ratio, so the choice of
// window floor cannot tint or brighten the image, only bound it.

struct LlfApplyParams
{
    uint2 size;
    float evPerUnit;   // the window's height, log2 units
    float maxEv;       // clamp on the correction
};

[shader("compute")]
[numthreads(16, 16, 1)]
void llfApply(uint3 tid : SV_DispatchThreadID,
              uniform Texture2D<float4>   src,
              uniform Texture2D<float>    filtered,
              uniform Texture2D<float>    original,
              uniform RWTexture2D<float4> dst,
              uniform ConstantBuffer<LlfApplyParams> p)
{
    if (tid.x >= p.size.x || tid.y >= p.size.y) return;

    const float4 c  = src[tid.xy];
    const float  ev = clamp((filtered[tid.xy] - original[tid.xy]) * p.evPerUnit,
                            -p.maxEv, p.maxEv);

    dst[tid.xy] = float4(c.rgb * exp2(ev), c.a);
}
