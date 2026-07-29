/*  The lens database: measured distortion and vignetting, by lens.
 *
 *  Orion's lens maths was already lensfun's published models; what was missing
 *  was the numbers. This reads them out of lensfun's own database — the XML
 *  files, not the library.
 *
 *  **Why the data and not the library.** The maths is fifteen lines and already
 *  written, tested and running on the GPU. Linking lensfun would add an LGPL-3
 *  dependency, a build step and a second implementation of the same
 *  polynomials, to obtain a value Orion can read directly. The database is
 *  CC BY-SA 3.0 and is vendored under `data/lensfun/` with its licence, exactly
 *  as published — attribution and share-alike apply to the data.
 *
 *  See research/lens-corrections.md.
 */

#pragma once

#include <string>
#include <vector>

namespace orion::pipe {

/// One lens's correction at one focal length and aperture.
struct LensProfile {
    bool found = false;

    /// The database entry that matched, for the interface to name.
    std::string lens;
    std::string maker;

    /// ptlens a, b, c. poly3's k₁ arrives as b with a and c zero, which is the
    /// same polynomial.
    float poly[3]{};

    /// lensfun's "pa" vignetting: V(r) = 1 + k₁·r² + k₂·r⁴ + k₃·r⁶.
    float vignette[3]{};

    /// True when the match came from a name the database spells differently —
    /// the interface says so rather than implying a measurement of this exact
    /// lens.
    bool approximate = false;
};

class LensDatabase {
public:
    /// Reads every XML file in `directory` once. Cheap enough to do at
    /// startup: the whole database is about five megabytes of text and parses
    /// in well under a tenth of a second, and the alternative is a stall the
    /// first time a photo is opened.
    explicit LensDatabase(const std::string& directory);

    /// The profile for a lens at this focal length and aperture, interpolated
    /// between the nearest calibrations.
    ///
    /// `lensName` is what the file's EXIF says, which is rarely spelled the way
    /// the database spells it — see `normalize` in the implementation for what
    /// is done about that.
    [[nodiscard]] LensProfile lookup(const std::string& lensName,
                                     float focalLength, float aperture) const;

    [[nodiscard]] std::size_t lensCount() const noexcept { return lenses_.size(); }
    [[nodiscard]] bool loaded() const noexcept { return !lenses_.empty(); }

    /// One measured point: coefficients at a focal length, and for vignetting
    /// an aperture too. Public so the interpolation can be a free function
    /// rather than a member the tests cannot reach.
    struct Calibration {
        float focal = 0.0f;
        float aperture = 0.0f;
        float coeff[3]{};
    };

private:

    struct Lens {
        std::string maker;
        std::string model;
        std::string normalized;
        std::vector<Calibration> distortion;
        std::vector<Calibration> vignetting;
    };

    std::vector<Lens> lenses_;

    void parse(const std::string& xml);
};

}  // namespace orion::pipe
