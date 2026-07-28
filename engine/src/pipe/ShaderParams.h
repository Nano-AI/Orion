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
};
static_assert(sizeof(Linearize) == 48);

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
    std::uint32_t _pad2[2];
    float         hueShift[8];
    float         satShift[8];
    float         lumShift[8];
};
static_assert(sizeof(LinearAdjust) == 144);

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

}  // namespace orion::pipe::params
