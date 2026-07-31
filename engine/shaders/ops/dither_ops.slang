// Ordered dithering (Bayer, 1973), 4x4.
//
// Eight bits is all the drawable holds, so quantising costs nothing on its own
// — but the geometry node then *resamples* those values and quantises again,
// and two roundings of a smooth gradient is exactly where contouring comes
// from. A night sky at 200% is the case that shows it. A sub-LSB offset that
// varies with position turns the contour into noise the eye integrates away; it
// is what every eight-bit display pipeline does, and it is free.
//
// ⚠ **This has to be applied by whichever node writes the eight bits, and by
// only that one.** It lived at the end of `develop_display.slang` until film
// grain moved the quantisation boundary: grain must be added to unquantised
// values, so `develop:display` writes float now and `develop:grain` is the node
// that rounds. Shared from here so there is one table rather than two copies
// that can drift.

#pragma once

static const float kBayer4x4[16] = {
     0.0f,  8.0f,  2.0f, 10.0f,
    12.0f,  4.0f, 14.0f,  6.0f,
     3.0f, 11.0f,  1.0f,  9.0f,
    15.0f,  7.0f, 13.0f,  5.0f,
};

/// The sub-LSB offset for this pixel, in eight-bit display units.
float bayerOffset(uint2 px)
{
    const uint i = (px.y & 3u) * 4u + (px.x & 3u);
    return (kBayer4x4[i] / 16.0f - 0.5f) / 255.0f;
}
