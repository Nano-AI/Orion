// Geometry — orientation, straightening, and crop, in one sampling pass.
//
// These three belong together because they are one coordinate transform. Doing
// them as separate nodes would resample the image three times and soften it
// three times over; composing the transform first and sampling once does not.
//
// Runs last in the graph, and is the only node whose output dimensions differ
// from its input.
//
// Sampling is bilinear on the *display-encoded* result, which is the correct
// place for it: resampling linear light and then encoding would brighten edges,
// because the average of two encoded values is not the encoding of their
// average.

struct GeometryParams
{
    uint2 outSize;
    uint2 inSize;

    uint  quarterTurns;   // 0..3, clockwise, camera orientation plus user
    float straightenRad;  // fine rotation, applied after the quarter turns
    float2 cropOrigin;    // normalized, in post-rotation space
    float2 cropSize;      // normalized
    float2 pivot;         // straighten center, normalized in post-rotation space
};

/// Bilinear fetch. Outside the frame returns transparent black rather than
/// clamping — clamping replicates the edge pixel along the rotation direction,
/// which is the streaking a straightened image showed before this.
float4 sampleBilinear(Texture2D<float4> src, float2 p, uint2 size)
{
    const float2 maxCoord = float2(size) - 1.0f;

    if (p.x < -0.5f || p.y < -0.5f || p.x > maxCoord.x + 0.5f || p.y > maxCoord.y + 0.5f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    p = clamp(p, float2(0.0f, 0.0f), maxCoord);

    const int2 i0 = int2(floor(p));
    const int2 i1 = min(i0 + int2(1, 1), int2(maxCoord));
    const float2 f = p - float2(i0);

    const float4 a = lerp(src[int2(i0.x, i0.y)], src[int2(i1.x, i0.y)], f.x);
    const float4 b = lerp(src[int2(i0.x, i1.y)], src[int2(i1.x, i1.y)], f.x);
    return lerp(a, b, f.y);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void geometry(uint3 tid : SV_DispatchThreadID,
              uniform Texture2D<float4>   src,
              uniform RWTexture2D<float4> dst,
              uniform ConstantBuffer<GeometryParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    // Rotated frame dimensions, before cropping.
    const bool swaps = (p.quarterTurns % 2u) != 0u;
    const float2 rotated = swaps ? float2(p.inSize.y, p.inSize.x) : float2(p.inSize);

    // Output pixel to a point in the cropped region, then to the full rotated
    // frame. Working backwards means every output pixel is written once.
    // The 0.5 offsets convert between pixel indices and continuous
    // coordinates: output pixel i covers [i, i+1) with its center at i+0.5, and
    // the source pixel at that center is index i, not i+0.5. Dropping the
    // second subtraction shifts the whole image half a pixel and softens it,
    // which is invisible by eye and obvious to a test.
    const float2 t = (float2(tid.xy) + 0.5f) / float2(p.outSize);
    float2 r = (p.cropOrigin + t * p.cropSize) * rotated - 0.5f;

    // Straighten about the center of the crop, not the frame — rotating about
    // the frame center would slide the composition out from under the crop.
    //
    // The pivot is passed in rather than derived from cropOrigin and cropSize,
    // because those two describe the *canvas* while the crop tool is open, not
    // the user's rectangle. Deriving it meant the preview turned about the
    // frame center and the committed render about the crop center, so an
    // off-center crop delivered a different picture than the box had shown.
    if (abs(p.straightenRad) > 1e-6f)
    {
        const float2 pivot = p.pivot * rotated;
        const float s = sin(p.straightenRad);
        const float c = cos(p.straightenRad);
        const float2 d = r - pivot;
        r = pivot + float2(d.x * c - d.y * s, d.x * s + d.y * c);
    }

    // Undo the quarter turns to reach the source pixel.
    const float2 inMax = float2(p.inSize) - 1.0f;
    float2 s;
    switch (p.quarterTurns)
    {
        case 1u:  s = float2(r.y, inMax.y - r.x); break;
        case 2u:  s = float2(inMax.x - r.x, inMax.y - r.y); break;
        case 3u:  s = float2(inMax.x - r.y, r.x); break;
        default:  s = r; break;
    }

    dst[tid.xy] = sampleBilinear(src, s, p.inSize);
}
