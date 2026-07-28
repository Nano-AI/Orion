/*  Host-side mirrors of the shader parameter blocks.
 *
 *  These MUST match the corresponding structs in the engine/shaders Slang sources byte
 *  for byte. Metal's rules: float4 aligns to 16, uint2 aligns to 8, and the
 *  struct is padded to its largest member's alignment. The static_asserts below
 *  catch a drift at compile time rather than as a mystery in the output image.
 */

#pragma once

#include <cstdint>

namespace orion::pipe::params {

struct alignas(16) Linearize {
    float         black[4];        // per CFA channel, sensor counts
    float         whiteBalance[4]; // per CFA channel, normalised to green
    float         invRange;        // 1 / (white - black)
    std::uint32_t filters;         // CFA bitmask
    std::uint32_t size[2];
    float         whiteClip;       // common saturation level, post white balance
    float         _pad[3];
};
static_assert(sizeof(Linearize) == 64);

struct Dirs {
    std::uint32_t size[2];
};
static_assert(sizeof(Dirs) == 8);

struct Green {
    std::uint32_t size[2];
    std::uint32_t filters;
    std::uint32_t _pad;
};
static_assert(sizeof(Green) == 16);

using RedBlue = Green;

struct alignas(16) ColorMatrix {
    float         row0[4];   // camera -> working, w unused
    float         row1[4];
    float         row2[4];
    std::uint32_t size[2];
    std::uint32_t _pad[2];
};
static_assert(sizeof(ColorMatrix) == 64);

/// Every scene-linear adjustment, fused into one dispatch.
struct LinearAdjust {
    float         exposureEv;
    float         highlights;
    float         shadows;
    float         whites;
    float         blacks;
    float         vibrance;
    float         saturation;
    float         _pad;
    std::uint32_t size[2];
    /// Dimensions of the subsampled guide coefficients, which this kernel lifts
    /// back to full resolution. See guide_down.slang.
    std::uint32_t guideSize[2];
    float         hueShift[8];
    float         satShift[8];
    float         lumShift[8];
};
static_assert(sizeof(LinearAdjust) == 144);

struct GuidePrep {
    std::uint32_t size[2];
    std::uint32_t _pad[2];
};
static_assert(sizeof(GuidePrep) == 16);

struct BoxBlur {
    std::uint32_t size[2];
    std::int32_t  radius;
    std::int32_t  horizontal;
};
static_assert(sizeof(BoxBlur) == 16);

struct GuideAb {
    std::uint32_t size[2];
    float         epsilon;
    float         _pad;
};
static_assert(sizeof(GuideAb) == 16);

struct Atrous {
    std::uint32_t size[2];
    std::int32_t  step;
    std::int32_t  _pad;
};
static_assert(sizeof(Atrous) == 16);

struct Shrink {
    std::uint32_t size[2];
    float         noiseA;
    float         noiseB;
    float         scaleNorm;
    float         strength;
    float         chromaBoost;
    float         _pad;
};
static_assert(sizeof(Shrink) == 32);

struct Sharpen {
    float         amount;
    float         radius;
    float         masking;
    float         _pad;
    std::uint32_t size[2];
    std::uint32_t _pad2[2];
};
static_assert(sizeof(Sharpen) == 32);

/// AgX plus the tone curve plus the display encode.
struct Display {
    float         contrast;
    float         pivot;
    std::uint32_t curveIdentity;
    std::uint32_t resolution;
    std::uint32_t size[2];
    std::uint32_t _pad[2];
};
static_assert(sizeof(Display) == 32);

/// Lens corrections. Mirrors LensParams in lens.slang.
struct Lens {
    std::uint32_t size[2];
    float centreX, centreY;
    float k1;
    float caRed, caBlue;
    float vignetteA;
    float _pad[4];
};
static_assert(sizeof(Lens) == 48);

/// Guide subsampling. Mirrors GuideDownParams in guide_down.slang.
struct GuideDown {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    std::int32_t  scale;
    std::int32_t  _pad[3];
};
static_assert(sizeof(GuideDown) == 32);

/// Highlight reconstruction. Mirrors HighlightParams in highlights.slang.
struct Highlights {
    std::uint32_t size[2];
    float clipR, clipG;
    float clipB, gamma;
    float strength;
    float _pad;
};
static_assert(sizeof(Highlights) == 32);

/// À-trous blur, one scale. Mirrors AtrousParams in denoise_blur.slang.
struct AtrousBlur {
    std::uint32_t size[2];
    std::int32_t  step;     // 2^j
    std::int32_t  _pad;
};
static_assert(sizeof(AtrousBlur) == 16);

/// Shrinkage at one scale. Mirrors ShrinkParams in denoise_accum.slang.
struct AtrousShrink {
    std::uint32_t size[2];
    float noiseA;
    float noiseB;
    float scaleNorm;
    float strength;
    float chromaBoost;
    float _pad;
};
static_assert(sizeof(AtrousShrink) == 32);

struct Geometry {
    std::uint32_t outSize[2];
    std::uint32_t inSize[2];
    std::uint32_t quarterTurns;
    float         straightenRad;
    float         cropOrigin[2];
    float         cropSize[2];
    /// Straighten centre, normalised in post-rotation frame space. Always the
    /// centre of the user's crop rectangle, whether or not the crop tool's
    /// enlarged preview canvas is in play.
    float         pivot[2];
};
static_assert(sizeof(Geometry) == 48);

}  // namespace orion::pipe::params
