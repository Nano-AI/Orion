/*  Reading a .cube LUT.
 *
 *  A reader for the whole format, not just the part Orion applies. The
 *  temptation with a file format is to parse only what the current feature
 *  needs and let the rest fail quietly; that is how a reader becomes something
 *  that works on the four files it was tested with. This one either understands
 *  a file or says which line it did not understand.
 *
 *  research/luts.md.
 */

#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace orion::pipe {

struct CubeLut {
    /// Edge length of the 3D grid. Always 3D once parsed — a 1D file is
    /// resampled onto a 3D grid on the way in, because a 1D LUT *is* a
    /// separable 3D LUT and one code path downstream is worth more than the
    /// memory a small grid costs.
    int size = 0;

    /// The input range the table is indexed over. Almost always 0..1, but the
    /// format allows otherwise and a log-encoded LUT is where it matters.
    std::array<float, 3> domainMin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> domainMax{1.0f, 1.0f, 1.0f};

    std::string title;

    /// size^3 entries of RGB, **red varying fastest**, then green, then blue.
    std::vector<float> data;

    /// True when this file was a 1D LUT that has been lifted onto the grid.
    bool wasOneDimensional = false;

    [[nodiscard]] bool valid() const noexcept {
        return size >= 2 &&
               data.size() == static_cast<std::size_t>(size) * size * size * 3;
    }
};

struct CubeLutResult {
    bool        ok = false;
    std::string error;     // human-readable, names the line when it can
    CubeLut     lut;
};

/// Parses the text of a .cube file.
///
/// The grid a 1D file is lifted onto. 33 is the size essentially every creative
/// LUT ships at, and for a separable function the grid is exact at its nodes.
inline constexpr int kOneDimensionalLift = 33;

/// Largest 3D edge accepted. The format permits more; this is the bound on the
/// texture the grid is uploaded into. 65 covers everything shipped in practice
/// — 17, 33 and 65 are the sizes creative LUTs come in — and a larger file is
/// refused by name rather than truncated into something that looks nearly right.
inline constexpr int kMaxCubeSize = 65;

[[nodiscard]] CubeLutResult parseCube(std::string_view text);

}  // namespace orion::pipe
