// Sensor samples -> normalized linear CFA.
//
// Subtracts the per-channel black point, scales to 0..1 against the saturation
// point, applies the as-shot white balance, and clips to the white level.
// Output is still a mosaic — one channel per pixel — but now scene-linear
// float.
//
// The clip is the part that matters. A sensor saturates at one count for every
// channel, so a blown highlight arrives as (S, S, S). White balance then
// multiplies each channel by its own gain — for a warm scene something like
// (2.2, 1.0, 1.6) — and what was a white light is now, in ratio, magenta.
// Nothing downstream can undo that: the tone curve and the display transform
// both preserve ratios, so they preserve the cast and paint it on every clipped
// light in the frame.
//
// Clipping all three channels to a single ceiling is what makes the blown pixel
// white again: (2.2, 1.0, 1.6) -> (1, 1, 1). This is dcraw's default highlight
// mode, and the ceiling is the *lowest* of the per-channel saturation levels,
// because that is the brightest neutral the sensor can still describe. A
// channel above it says "more of this primary than a white at full brightness",
// which is not a color the white point admits.
//
// Reference: Coffin, dcraw `scale_colors()`, highlight mode 0; the same
// convention LibRaw inherits. Restated in research/color-pipeline.md.
//
// It has to happen here, before demosaic, for the same reason dcraw does it
// here: RCD interpolates the mosaic, so an unclipped neighbor at 2.2 would
// drag the estimate at every pixel around it, and the cast would spread past
// the highlight as a fringe instead of stopping at its edge.

#include "cfa.slang"

struct LinearizeParams
{
    float4 black;        // per CFA channel, in sensor counts
    float4 whiteBalance; // per CFA channel, normalized to green
    float  invRange;     // 1 / (white - black)
    uint   filters;      // CFA pattern bitmask
    uint2  size;
    float  whiteClip;    // common saturation level, post white balance
    float  _pad[3];
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

    dst[tid.xy] = clamp(v, 0.0f, p.whiteClip);
}
