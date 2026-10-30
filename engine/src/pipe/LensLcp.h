/*  Adobe lens profiles (.lcp) the photographer already has installed.
 *
 *  An LCP file is XMP — RDF/XML in the `stCamera` namespace — holding one or
 *  more sub-profiles, each a camera/lens/focal length/aperture with a
 *  `PerspectiveModel` (the Adobe Camera Model: Brown–Conrady radial k1..k3 and
 *  tangential k4, k5, in units of the focal length) and optionally a
 *  `VignetteModel` (1 + α1·r² + α2·r⁴ + α3·r⁶, divided out).
 *
 *  ⚠ **Read from the user's own Adobe folders, never bundled.** Adobe's
 *  profiles are Adobe's; Orion reads what Camera Raw or Lightroom already put
 *  on this machine, the same as it would read any other file the user owns.
 *
 *  Implemented from the published format description, not from any other
 *  reader's code — RawTherapee reads LCP files too and is GPL; it is prior art
 *  here, not a source. See research/lens-corrections.md.
 */

#pragma once

#include "pipe/LensDatabase.h"

#include <string>
#include <vector>

namespace orion::pipe {

class AdobeLensProfiles {
public:
    /// Reads every `.lcp` under each directory, recursively. Missing
    /// directories are simply empty — most machines have no Adobe software.
    explicit AdobeLensProfiles(const std::vector<std::string>& directories);

    /// Parses one file's text. Public so a test can hand it a fixture.
    void addText(const std::string& xml);

    /// The two places Camera Raw and Lightroom install profiles on macOS: the
    /// user's own Library first, then the machine's.
    [[nodiscard]] static std::vector<std::string> defaultDirectories();

    /// The profile for this camera and lens at this focal length and aperture.
    ///
    /// ⚠ **The camera body must match**, not only the lens. Adobe's model
    /// normalizes by the calibration body's larger image dimension, so a lens
    /// profile measured on a full-frame body is wrong by the crop factor on an
    /// APS-C one, and the photo does not say which sensor it came from in a
    /// form this could rescale by. A fixed-lens camera — every DJI drone — is
    /// matched by body alone; an interchangeable one also needs the lens name
    /// to agree whenever both sides record one.
    [[nodiscard]] LensProfile lookup(const std::string& camera, const std::string& lens,
                                     float focalLength, float aperture) const;

    [[nodiscard]] std::size_t size() const noexcept { return subs_.size(); }

    /// One sub-profile. `persp` and `vig` hold FocalLengthX, FocalLengthY,
    /// ImageXCenter, ImageYCenter, then the model's own parameters (k1, k2,
    /// k3, k4, k5 for the perspective model; α1, α2, α3 for vignetting).
    struct Sub {
        std::size_t file = 0;
        std::string camera;      // normalized Make + Model
        std::string lens;        // normalized Lens, may be empty
        std::string name;        // LensPrettyName, else Lens, for the interface
        float focal = 0.0f, aperture = 0.0f, distance = 0.0f;
        bool  hasPersp = false, hasVig = false;
        float persp[9]{};
        float vig[7]{};
    };

private:
    std::vector<Sub> subs_;
    std::size_t files_ = 0;
};

}  // namespace orion::pipe
