/*  DNG writer — linear-RGB floating-point DNG 1.4, written from the spec.
 *
 *  This exists for HDR merge: the merged radiance map needs a container that
 *  (a) LibRaw reads back through the same door an ARW comes in, so the merged
 *  file is a normal library photo, and (b) keeps white balance editable, which
 *  means storing camera-native RGB plus the color tags rather than a rendered
 *  space.
 *
 *  ⚠ Implemented from the Adobe DNG 1.4 specification and TIFF 6.0 — not from
 *  hdrmerge's DngFloatWriter, which is GPLv3 and itself derived from
 *  dngconvert. See research/hdr-merge.md for the section-by-section sources.
 *
 *  Scope is deliberately narrow: LinearRaw (PhotometricInterpretation 34892),
 *  three half-float samples per pixel, uncompressed, one IFD. Compression and
 *  previews are later stories; a writer that does one layout exactly beats one
 *  that does four approximately.
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace orion::util {

struct DngLinearImage {
    std::uint32_t width  = 0;
    std::uint32_t height = 0;

    /// Interleaved RGB, row-major, tightly packed (width * 3 floats per row).
    /// Scene-linear camera-native RGB, normalized so 1.0 is the encoded
    /// ceiling. Values are clamped to [0, 1] at write time: the headroom above
    /// the reference exposure is carried by BaselineExposure, not by samples
    /// past the white level.
    const float* rgb = nullptr;

    /// ColorMatrix1: XYZ (D65) -> camera RGB, row-major. This is LibRaw's
    /// cam_xyz exactly as BayerImage.camToXyz stores it — same direction, no
    /// inversion. The name on BayerImage is historical; see RawImage.cpp:217.
    std::array<float, 9> xyzToCam{};

    /// AsShotNeutral: the camera-space color of a neutral patch, i.e. the
    /// reciprocals of the white-balance gains, normalized so green is 1.0.
    std::array<float, 3> asShotNeutral{1.0f, 1.0f, 1.0f};

    /// log2 of the gain a reader should apply to render at the intended
    /// brightness. For a merge normalized to its ceiling this is +log2(H)
    /// where H is the headroom bought by the shortest exposure.
    float baselineExposureEv = 0.0f;

    /// UniqueCameraModel, and the TIFF Make/Model pair (split on first space).
    std::string camera;

    /// LibRaw's flip convention, as BayerImage/LinearImage carry it: 0 none,
    /// 3 = 180, 5 = 90 CCW, 6 = 90 CW. Written as the TIFF Orientation tag,
    /// so a portrait bracket merges to a portrait DNG — the reader turns it
    /// back into the same flip and the canvas orients as it does every raw.
    int flip = 0;

    /// An optional embedded JPEG preview — what the filmstrip and Finder
    /// show without decoding a quarter-gigabyte of half floats. When set,
    /// the file takes the standard DNG shape: the preview and the camera
    /// metadata in IFD0 (NewSubfileType 1), the raw image in a SubIFD.
    /// ⚠ The pixels must already be upright: the preview's own orientation
    /// is written as 1, because thumbnail consumers (LibRaw's unpack_thumb,
    /// QuickLook) hand the JPEG stream around without the TIFF tag it sat
    /// under. The raw SubIFD keeps the real orientation.
    const std::uint8_t* previewJpeg = nullptr;
    std::size_t  previewJpegBytes = 0;
    std::uint32_t previewWidth  = 0;
    std::uint32_t previewHeight = 0;
};

/// Writes the DNG. Throws std::runtime_error on failure; on any throw the
/// path is left absent rather than holding a truncated file.
void writeDngLinear(const std::string& path, const DngLinearImage& image);

}  // namespace orion::util
