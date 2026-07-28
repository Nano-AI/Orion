// Shared color-filter-array helpers.

#ifndef ORION_CFA_SLANG
#define ORION_CFA_SLANG

// Which CFA channel sits at p. Mirrors LibRaw's FC macro, and
// orion::raw::BayerImage::channelAt on the CPU side — keep the three in step.
// 0 = R, 1 = G, 2 = B, 3 = G (second green).
uint cfaChannel(uint2 p, uint filters)
{
    const uint shift = (((p.y << 1) & 14) | (p.x & 1)) << 1;
    return (filters >> shift) & 3;
}

// True when p carries a green sample.
bool isGreen(uint2 p, uint filters)
{
    const uint c = cfaChannel(p, filters);
    return c == 1 || c == 3;
}

// CFA channel with the second green folded onto the first, so callers can
// compare against 0 = R, 1 = G, 2 = B without special-casing.
uint cfaChannelRGB(uint2 p, uint filters)
{
    const uint c = cfaChannel(p, filters);
    return (c == 3) ? 1 : c;
}

// Clamp a coordinate into the image, so kernels can read neighbours near the
// border without branching on every tap.
int2 clampToImage(int2 p, uint2 size)
{
    return int2(clamp(p.x, 0, int(size.x) - 1),
                clamp(p.y, 0, int(size.y) - 1));
}

// Border-clamped read. Slang has no nested functions, so neighbourhood kernels
// share this rather than each defining a local tap helper.
float tapClamped(Texture2D<float> tex, int2 p, uint2 size)
{
    return tex[clampToImage(p, size)];
}

#endif
