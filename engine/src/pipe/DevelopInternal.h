/*  What the DevelopPipeline stage files share.
 *
 *  Private to `engine/src/pipe`; nothing outside it includes this, and in
 *  particular the POD facade does not. Decision #113 split
 *  `DevelopPipeline.cpp` five ways, and this is the whole list of things that
 *  turned out to belong to more than one of the pieces — which is one
 *  constant, read by the mask group's luminance band and by the tone node.
 */

#pragma once

namespace orion::pipe {

/// How far Orion's zero point sits below every other converter's.
///
/// **Measured, not chosen.** Orion opened a daylight frame about 1.3x darker in
/// the midtones than the camera's own JPEG, which read as flat and washed. A
/// two-dimensional fit of exposure against base contrast, scored as mean
/// absolute luma error over six patches spanning each frame's tonal range,
/// against two independent references (the camera's JPEG and Apple's RAW
/// rendering):
///
///     _PIC8095 daylight   best +1.20 EV, contrast 1.45, error 0.0171
///     _PIC8220 forecourt  best +1.20 EV, contrast 1.45, error 0.0103
///     _PIC8148 night sky  best +1.60 EV, contrast 2.05, error 0.0068
///
/// Two of three agree exactly. The night frame's error surface is nearly flat —
/// 0.0083 at the old defaults against 0.0068 at its own minimum — because a
/// near-black frame barely moves a mean luma, so its preference is noise. At
/// (+1.2, 1.45) its error is 0.0150. Consistent wherever there is signal.
///
/// The mechanism is the DNG specification's `BaselineExposure` (tag 50730),
/// "by how much (in EV units) to move the zero point", which Adobe applies
/// silently on open — which is why the user's Exposure slider still reads 0.00
/// here rather than +1.20. See research/camera-profiles.md.
///
/// ⚠️ **What this value is not yet known to be.** It fits one camera body. A
/// per-camera `BaselineExposure` and a property of Orion's own AgX zero point
/// are indistinguishable from a single body's data, and LibRaw does not carry
/// the tag for native ARW. The moment a second body is supported, measure it
/// again: if the number moves, it is per-camera and belongs in a table; if it
/// does not, it belongs in the display transform.
inline constexpr float kBaselineExposureEv = 1.2f;

}  // namespace orion::pipe
