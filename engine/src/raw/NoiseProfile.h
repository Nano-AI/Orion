/*  Poisson–Gaussian noise profile, measured from the mosaic itself.
 *
 *  Reference: Foi, Trimeche, Katkovnik & Egiazarian (2008), "Practical
 *  Poissonian-Gaussian Noise Modeling and Fitting for Single-Image Raw-Data",
 *  IEEE Transactions on Image Processing 17(10), 1737-1754.
 *
 *  The model is
 *
 *      var(x) = a·x + b
 *
 *  with a·x the photon shot noise, which grows with the signal, and b the read
 *  noise, dark current and amplifier noise, which does not. Fitting both from
 *  the frame in front of us rather than from a per-camera table means the
 *  profile is right for this exposure and this ISO without a database to keep
 *  up to date — and darktable's table, which is the obvious source for one, is
 *  GPL and therefore off limits here.
 *
 *  The estimate is made on the mosaic, before white balance and before any
 *  color transform, because that is the only place the model holds: both
 *  scale the channels by different amounts and would scale their variances
 *  with them.
 */

#pragma once

#include "raw/RawImage.h"

namespace orion::raw {

struct NoiseProfile {
    /// Signal-dependent term, in normalized units where 1.0 is saturation.
    float a = 0.0f;
    /// Constant term, same units.
    float b = 0.0f;

    /// True when the fit had enough data to be worth believing. A frame that
    /// is almost entirely clipped, or tiny, will not.
    bool measured = false;
};

/// Fits the model to a mosaic.
///
/// Same-color neighbours two pixels apart are *second*-differenced, which
/// cancels the scene and leaves noise. Second rather than first because a first
/// difference measures the local gradient as though it were noise — on a frame
/// with a sky in it that is most of what it measures. A second difference
/// annihilates a linear ramp exactly, and var(p - 2q + r) = 6·var(p).
/// The differences are binned by brightness into bins of equal *population*
/// rather than equal width — a night sky puts every pixel in the bottom twelfth
/// of the range, and equal-width bins simply came back empty, so the fit was
/// abandoned on exactly the frames that needed denoising most. Each bin's
/// spread is a median absolute deviation rather than a standard deviation, so
/// an edge or a star inside the bin moves the answer very little, and a
/// weighted least squares through the bins gives a and b.
///
/// A frame with almost no brightness range gives no lever arm for the slope. In
/// that case a is left at zero and b carries the whole model, which is still
/// correct for that frame even though it is not a general profile.
[[nodiscard]] NoiseProfile estimateNoise(const BayerImage&);

}  // namespace orion::raw
