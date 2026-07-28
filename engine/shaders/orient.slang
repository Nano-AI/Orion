// Orientation — the camera's EXIF rotation plus whatever the user adds.
//
// A portrait frame shot on a landscape sensor arrives as landscape pixels with
// a flag saying "turn this". Every editor has to honour that flag or portraits
// show up sideways.
//
// This is the last node in the graph, and the only one whose output dimensions
// differ from its input: a quarter turn swaps width and height.

struct OrientParams
{
    uint2 outSize;
    uint2 inSize;
    uint  quarterTurns;   // 0, 1, 2, 3 — clockwise
    uint  _pad[3];
};

[shader("compute")]
[numthreads(16, 16, 1)]
void orient(uint3 tid : SV_DispatchThreadID,
            uniform Texture2D<float4>   src,
            uniform RWTexture2D<float4> dst,
            uniform ConstantBuffer<OrientParams> p)
{
    if (tid.x >= p.outSize.x || tid.y >= p.outSize.y) return;

    const uint ox = tid.x;
    const uint oy = tid.y;

    // Map each output pixel back to its source. Working backwards like this
    // means every output pixel is written exactly once, with no gaps.
    uint2 s;
    switch (p.quarterTurns)
    {
        case 1u:   // 90 clockwise
            s = uint2(oy, p.inSize.y - 1u - ox);
            break;
        case 2u:   // 180
            s = uint2(p.inSize.x - 1u - ox, p.inSize.y - 1u - oy);
            break;
        case 3u:   // 270 clockwise, i.e. 90 anticlockwise
            s = uint2(p.inSize.x - 1u - oy, ox);
            break;
        default:
            s = uint2(ox, oy);
            break;
    }

    s = min(s, p.inSize - uint2(1u, 1u));
    dst[tid.xy] = src[s];
}
