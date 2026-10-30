/*  Adobe lens profiles (.lcp) the photographer already has installed.
 *
 *  An LCP file is XMP (RDF/XML, the `stCamera` namespace) holding sub-profiles
 *  for one camera body and lens, each at a focal length, aperture and focus
 *  distance, with a `PerspectiveModel` (the Adobe Camera Model: Brown–Conrady
 *  radial k1..k3 and tangential k4, k5, in units of the focal length) and
 *  optionally a `VignetteModel` (1 + α1·r² + α2·r⁴ + α3·r⁶, divided out).
 *
 *  ⚠ **Read from the user's own Adobe folders, never bundled.** Adobe's
 *  profiles are Adobe's; Orion reads what Camera Raw or Lightroom already put
 *  on this machine.
 *
 *  ⚠ **Nothing here may hold up an open.** A full Camera Raw install is a few
 *  thousand files, so construction starts a background thread that only learns
 *  each file's camera body (streaming, stopping at the first Make and Model).
 *  A file is parsed in full the first time its body is looked up, then cached.
 *  Until the index is complete `lookup` reports nothing found, and that photo
 *  gets lensfun.
 *
 *  Parsed with libxml2. Implemented from the published format description, not
 *  from any other reader's code. RawTherapee reads LCP files too and is GPL;
 *  it is prior art here, not a source. See research/lens-corrections.md.
 */

#pragma once

#include "pipe/LensDatabase.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace orion::pipe {

class AdobeLensProfiles {
public:
    /// Starts indexing every `.lcp` under each directory, recursively, in the
    /// background. Missing directories are empty: most machines have no Adobe
    /// software.
    explicit AdobeLensProfiles(std::vector<std::string> directories);
    ~AdobeLensProfiles();
    AdobeLensProfiles(const AdobeLensProfiles&) = delete;
    AdobeLensProfiles& operator=(const AdobeLensProfiles&) = delete;

    /// The two places Camera Raw and Lightroom install profiles on macOS: the
    /// user's own Library first, then the machine's.
    [[nodiscard]] static std::vector<std::string> defaultDirectories();

    [[nodiscard]] bool ready() const noexcept { return ready_.load(); }
    /// Blocks until the index is complete. For tests: the product never waits.
    void waitUntilReady() const;

    /// One camera body, spelled the same way whoever wrote it. LibRaw says
    /// `Nikon` / `D850`; an LCP says `NIKON CORPORATION` / `NIKON D850`. Both
    /// become `nikon|d850`: the make's first word is the brand, and a model
    /// that repeats the brand has it removed.
    [[nodiscard]] static std::string bodyKey(const std::string& make, const std::string& model);

    /// The profile for this camera and lens at this focal length and aperture.
    /// Nothing found while the index is still being built.
    ///
    /// ⚠ **The camera body must match**, not only the lens: Adobe's model is
    /// normalized by the calibration body's larger image dimension, so a
    /// full-frame profile is wrong by the crop factor on an APS-C body. Where
    /// both the photo and a profile record a lens name they must agree. A
    /// photo with no lens name takes only a profile with none either, or the
    /// body's one file if it has only one — never "the first lens in sorted
    /// order".
    [[nodiscard]] LensProfile lookup(const std::string& make, const std::string& model,
                                     const std::string& lens, float focalLength,
                                     float aperture) const;

    /// One sub-profile. `persp` and `vig` hold FocalLengthX, FocalLengthY,
    /// ImageXCenter, ImageYCenter, then the model's own parameters (k1, k2,
    /// k3, k4, k5 for the perspective model; α1, α2, α3 for vignetting).
    struct Sub {
        std::string body;        // bodyKey(Make, Model)
        std::string lens;        // normalized Lens, may be empty
        std::string name;        // LensPrettyName, else Lens, for the interface
        float focal = 0.0f, aperture = 0.0f, distance = 0.0f;
        bool  hasVig = false;
        float persp[9]{};
        float vig[7]{};
    };

private:
    void index();
    const std::vector<Sub>& parsed(const std::string& path) const;

    std::vector<std::string> directories_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    mutable std::condition_variable readyChanged_;
    std::map<std::string, std::vector<std::string>> bodies_;   // body key -> files, sorted
    mutable std::map<std::string, std::vector<Sub>> cache_;     // file -> its sub-profiles
    std::thread indexer_;
};

}  // namespace orion::pipe
