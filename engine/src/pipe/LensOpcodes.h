/*  The lens correction a DNG carries in its own opcode lists.
 *
 *  Cameras that have no lensfun entry — every DJI drone, many phones — write
 *  their correction into the file as DNG `WarpRectilinear` (opcode 1) and
 *  `FixVignetteRadial` (opcode 3), Adobe DNG Specification 1.6, chapter 7.
 *  LibRaw hands over the three lists as-is (`dng_levels.rawopcodes`); this
 *  reads the two lens opcodes out of them and ignores everything else.
 *
 *  ⚠ The bytes come from the file, so every read is bounds-checked: a list
 *  that lies about its own lengths yields "nothing found", never a read past
 *  the buffer. Opcode parsers are a known attack surface (CVE-2025-21043).
 *
 *  See research/lens-corrections.md.
 */

#pragma once

#include "pipe/LensGeometry.h"

#include <cstdint>
#include <vector>

namespace orion::pipe {

struct DngLensOpcodes {
    bool hasWarp = false;
    bool hasVignette = false;
    LensModel model{};   // identity in whichever half was not found
};

/// Reads WarpRectilinear and FixVignetteRadial out of OpcodeList1/2/3.
///
/// The first of each is used. The vignette is placed on the output position
/// only when it follows the warp in processing order (list 1, 2, 3, each in
/// sequence) — before it, it ran on the unwarped image, the source position. A WarpRectilinear that
/// directly follows an *optional* WarpRectilinear2 (opcode 14) is still used:
/// the specification's skip rule binds readers that apply WarpRectilinear2,
/// and Orion does not.
[[nodiscard]] DngLensOpcodes parseDngLensOpcodes(const std::vector<std::uint8_t> lists[3]);

}  // namespace orion::pipe
