/*  IEEE 754 binary16 conversion, both directions, header-only.
 *
 *  Shared between the DNG writer (quantizing merge output) and the raw
 *  decoder (packing LibRaw's float planes for an RGBA16Float upload). Bit
 *  manipulation rather than frexp so the result has no libm variability —
 *  tests assert exact encodings.
 */

#pragma once

#include <cstdint>
#include <cstring>

namespace orion::util {

/// binary32 -> binary16, round to nearest even.
[[nodiscard]] inline std::uint16_t floatToHalf(float value) noexcept {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);

    const std::uint32_t sign     = (bits >> 16) & 0x8000u;
    const std::uint32_t exponent = (bits >> 23) & 0xffu;
    const std::uint32_t mantissa = bits & 0x7fffffu;

    if (exponent == 0xffu) {  // inf or NaN: keep the class, keep a payload bit
        return static_cast<std::uint16_t>(
            sign | 0x7c00u | (mantissa ? 0x0200u : 0u));
    }

    // Re-bias 127 -> 15. e is the unclamped half exponent.
    const int e = static_cast<int>(exponent) - 127 + 15;

    if (e >= 31) return static_cast<std::uint16_t>(sign | 0x7c00u);  // overflow -> inf
    if (e <= 0) {
        // Subnormal half, or underflow to zero. Shift the implicit-1 mantissa
        // right and round; below 2^-24 everything rounds to zero.
        if (e < -10) return static_cast<std::uint16_t>(sign);
        const std::uint32_t m     = mantissa | 0x800000u;
        const int           shift = 14 - e;
        std::uint32_t       half  = m >> shift;
        const std::uint32_t rest  = m & ((1u << shift) - 1u);
        const std::uint32_t point = 1u << (shift - 1);
        if (rest > point || (rest == point && (half & 1u))) ++half;
        return static_cast<std::uint16_t>(sign | half);
    }

    std::uint32_t half = (static_cast<std::uint32_t>(e) << 10) | (mantissa >> 13);
    const std::uint32_t rest = mantissa & 0x1fffu;
    if (rest > 0x1000u || (rest == 0x1000u && (half & 1u))) ++half;  // carry into exponent is correct rounding
    return static_cast<std::uint16_t>(sign | half);
}

/// binary16 -> binary32, exact.
[[nodiscard]] inline float halfToFloat(std::uint16_t h) noexcept {
    const std::uint32_t sign = (std::uint32_t(h) & 0x8000u) << 16;
    std::uint32_t exponent   = (h >> 10) & 0x1fu;
    std::uint32_t mantissa   = h & 0x3ffu;

    std::uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;  // signed zero
        } else {
            // Subnormal: renormalize into the wider exponent range.
            int e = -1;
            do { ++e; mantissa <<= 1; } while ((mantissa & 0x400u) == 0);
            bits = sign | (std::uint32_t(127 - 15 - e) << 23) |
                   ((mantissa & 0x3ffu) << 13);
        }
    } else if (exponent == 0x1fu) {
        bits = sign | 0x7f800000u | (mantissa << 13);  // inf / NaN
    } else {
        bits = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
    }
    float out;
    std::memcpy(&out, &bits, sizeof out);
    return out;
}

}  // namespace orion::util
