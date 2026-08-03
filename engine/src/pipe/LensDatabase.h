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

    /// Every lens the database carries, as "Maker Model", sorted and unique.
    ///
    /// ⚠ **This exists so a photographer can choose one, and choosing is the
    /// whole point.** `lookup` deliberately refuses a near-miss — a DG DN lens
    /// must never match a DG HSM entry, because applying one optical design's
    /// distortion to another's picture is worse than applying none — so when a
    /// lens is genuinely absent from the data there is nothing automatic left
    /// to try. The developer's own lens is one: the file names it correctly and
    /// the bundled database has no `Art 023` entry at all (#144). A list the
    /// interface can search is the only honest answer to that, because the
    /// person holding the camera knows what is on it and the file does not.
    [[nodiscard]] std::vector<std::string> names() const;

    /// The profile for a lens chosen **by name, exactly**, rather than matched
    /// from EXIF.
    ///
    /// ⚠ **Exact, and never a containment test.** `lookup`'s longest-containment
    /// search exists to survive EXIF spellings nobody controls; this takes a
    /// string that came *out* of `names()`, so anything but an exact hit is a
    /// bug rather than a spelling. Reports `found = false` rather than guessing.
    ///
    /// `approximate` is always false here: the photographer picked it, so the
    /// interface must not tell them it is a near match for something they chose
    /// deliberately.
    [[nodiscard]] LensProfile lookupExact(const std::string& makerModel,
                                          float focalLength,
                                          float aperture) const;

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

    /// ⚠ Public only so `vignetteAt` can be a free function shared by both
    /// lookup paths rather than a member each duplicates. Nothing outside this
    /// file constructs one.
    struct Lens {
        std::string maker;
        std::string model;
        std::string normalized;
        std::vector<Calibration> distortion;
        std::vector<Calibration> vignetting;
    };

private:

    std::vector<Lens> lenses_;

    void parse(const std::string& xml);
};

}  // namespace orion::pipe
