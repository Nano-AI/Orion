/*  Calibrate — master dark and master flat, applied to each light before it
 *  is registered. research/stacking.md §7, after [SIRIL]'s preprocessing.
 *
 *  A Bayer frame calibrates on its CFA mosaic, before demosaic — demosaic
 *  mixes neighbours, so a hot pixel subtracted afterwards would already be
 *  smeared over its 3x3. A linear DNG (already demosaiced) calibrates on its
 *  RGB samples. Either way a frame is a flat array of black-subtracted floats
 *  and a parallel array naming each sample's channel (channelAt for a mosaic,
 *  i % 3 for RGB), and the masters are arrays of the same shape.
 *
 *  ponytail: no flat-darks. The flat is corrected by the lights' master dark
 *  when one is given; a flat shot at a very different exposure than the lights
 *  keeps a small bias-and-dark-current error. Add a separate flat-dark list if
 *  real flats show it.
 *
 *  The masters' constants (gain 1 where the flat has no signal) are ours:
 *  research/UNSOURCED.md §22.
 */

#pragma once

#include "raw/RawImage.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orion::stack {

/// Per-sample median of equally sized frames (the master dark, and the flat before normalization).
/// An even count takes the mean of the two middle values. Empty in, empty out.
[[nodiscard]] std::vector<float> medianOf(const std::vector<std::vector<float>>& frames);

/// (flat − dark) normalized so each channel's mean is 1; channel[i] names sample i's channel (0..3).
/// dark may be empty. Samples ≤ 0 get gain 1 and are left out of their channel's mean.
[[nodiscard]] std::vector<float> flatGain(const std::vector<float>& flat, const std::vector<float>& dark,
                                          const std::vector<std::uint8_t>& channel);

/// v = (v − dark) / gain, in place; either master may be empty.
void calibrate(float* samples, std::size_t n, const std::vector<float>& dark, const std::vector<float>& gain);

/// Bayer: samples minus per-channel black, as float; and back (round, clamp to [black, white]).
[[nodiscard]] std::vector<float> mosaicOf(const raw::BayerImage& image);
void storeMosaic(raw::BayerImage& image, const std::vector<float>& mosaic);

/// Each mosaic sample's CFA channel (channelAt), for flatGain.
[[nodiscard]] std::vector<std::uint8_t> channelsOf(const raw::BayerImage& image);

}  // namespace orion::stack
