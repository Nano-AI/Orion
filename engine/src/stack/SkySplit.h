/*  SkySplit — the shell's sky matte carried onto the sensor frame.
 *
 *  The matte is drawn on the picture as displayed, with the EXIF turn
 *  applied; the stacker integrates the unturned sensor frame. So: undo the
 *  turn (raw::quarterTurnsFor, the tree's one mapping; the shell's convention,
 *  SubjectMatte.cs UndoTurns, is "a clockwise turn sent (x, y) to (h-1-y, x)"),
 *  bilinear-resample to the frame, and feather the seam.
 *
 *  Ours, not published (research/UNSOURCED.md §32): the 1%-of-long-edge
 *  feather radius, two box passes.
 */

#pragma once

#include <cstdint>
#include <vector>

namespace orion::stack {

/// An upright matte (as displayed: the EXIF turn applied) to the sensor frame: undo quarterTurnsFor(flip),
/// bilinear-resample to width x height, feather with a box blur of radius 1% of the long edge (two passes).
/// Returns width*height floats in 0..1, 1 = sky.
[[nodiscard]] std::vector<float> skyMatteToFrame(const float* upright, int mw, int mh,
                                                 std::uint32_t width, std::uint32_t height, int flip);

}  // namespace orion::stack
