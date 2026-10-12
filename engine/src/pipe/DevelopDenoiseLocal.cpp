/*  Masked denoise: a second, short wavelet chain where a mask can reach it.
 *
 *  The capture chain runs in camera RGB, before the matrix, and the mask fold
 *  reads the picture after it — so a mask cannot drive that chain without a
 *  cycle. This one runs after `profile:hue/sat`, reads the refined folds
 *  through `denoise:field`, and feeds dehaze. `guide:prep` and the mask
 *  components stay on `profile:hue/sat`, which is what keeps the graph acyclic.
 *  research/denoise-masked.md §2.
 */
#include "pipe/DevelopPipeline.h"

#include "pipe/ShaderParams.h"

#include <algorithm>
#include <iterator>
#include <string>

namespace orion::pipe {

void DevelopPipeline::buildLocalDenoiseNodes() {
    using gpu::PixelFormat;

    // β_luma, β_chroma per pixel from the eight refined folds. Reads the
    // refine outputs, not the components: a disabled refine resolves back to
    // its component, so either way this sees the coverage the overlay draws.
    nDenoiseField_ = pipeline_.add({"local denoise:field", "denoiseField",
                                    {nMaskRefine_[0], nMaskRefine_[1],
                                     nMaskRefine_[2], nMaskRefine_[3],
                                     nMaskRefine_[4], nMaskRefine_[5],
                                     nMaskRefine_[6], nMaskRefine_[7]},
                                    PixelFormat::RG16Float, {}, {}});

    // The capture chain's wiring exactly (`buildCaptureNodes`), on the working
    // picture. Every node's first input is the picture side, so a disabled
    // chain resolves straight back to `profile:hue/sat`.
    for (int j = 0; j < kDenoiseScales; ++j) {
        const int input = (j == 0) ? nHueSat_ : nLocalBlur_[j - 1];
        nLocalBlur_[j] = pipeline_.add({"local denoise:blur " + std::to_string(j),
                                        "atrousBlur", {input},
                                        PixelFormat::RGBA16Float, {}, {}});
    }
    for (int j = kDenoiseScales - 1; j >= 0; --j) {
        const int fine   = (j == 0) ? nHueSat_ : nLocalBlur_[j - 1];
        const int coarse = nLocalBlur_[j];
        const int accum  = (j == kDenoiseScales - 1) ? nLocalBlur_[j]
                                                     : nLocalShrink_[j + 1];
        nLocalShrink_[j] = pipeline_.add({"local denoise:shrink " + std::to_string(j),
                                          "atrousShrink",
                                          {fine, coarse, accum, nDenoiseField_},
                                          PixelFormat::RGBA16Float, {}, {}});
    }
}

void DevelopPipeline::applyLocalDenoise(const Adjustments& adj,
                                        const ApplyContext& ctx) {
    std::int32_t layerMask[kMaxMaskComponents];
    const int layerCount = layerTable(adj, layerMask);

    bool anyLayer = false;
    for (int L = 0; L < layerCount; ++L) {
        const auto& e = adj.layers[std::size_t(L)];
        anyLayer = anyLayer || e.denoiseLuma > 0.0f || e.denoiseColor > 0.0f;
    }
    // Nine nodes, off unless something asks for them: a layer at zero gets a
    // disabled chain, not one running an identity (§2.2).
    const bool on = noise_.measured && adj.maskCount > 0 && anyLayer;

    // Coverage is not here on purpose: a fold that moves dirties the field
    // through the graph. What is here is what the pushed blocks are made of.
    bool moved = ctx.first ||
                 adj.maskCount != lastAdj_.maskCount ||
                 adj.denoiseDetail != lastAdj_.denoiseDetail ||
                 adj.wb.temperatureK != lastAdj_.wb.temperatureK ||
                 adj.wb.tint != lastAdj_.wb.tint;
    for (int i = 0; i < kMaxMaskComponents && !moved; ++i) {
        const auto& a = adj.layers[std::size_t(i)];
        const auto& b = lastAdj_.layers[std::size_t(i)];
        moved = a.denoiseLuma != b.denoiseLuma || a.denoiseColor != b.denoiseColor ||
                adj.maskComponents[std::size_t(i)].startsLayer !=
                    lastAdj_.maskComponents[std::size_t(i)].startsLayer;
    }
    if (!moved) return;

    // Size only, so once per photograph, as `applyDenoise` does.
    if (ctx.first) {
        for (int j = 0; j < kDenoiseScales; ++j) {
            params::AtrousBlur blur{};
            blur.size[0] = width_;
            blur.size[1] = height_;
            blur.step = 1 << j;
            pipeline_.setParams(nLocalBlur_[j], &blur, sizeof blur);
        }
    }

    pipeline_.setEnabled(nDenoiseField_, on);
    for (int j = 0; j < kDenoiseScales; ++j) {
        pipeline_.setEnabled(nLocalBlur_[j], on);
        pipeline_.setEnabled(nLocalShrink_[j], on);
    }
    if (!on) return;

    params::DenoiseField field{};
    field.size[0] = width_;
    field.size[1] = height_;
    std::copy(std::begin(layerMask), std::end(layerMask), field.layerMask);
    field.layerCount = layerCount;
    for (int L = 0; L < kMaxMaskComponents; ++L) {
        field.layerLuma[L]   = adj.layers[std::size_t(L)].denoiseLuma;
        field.layerChroma[L] = adj.layers[std::size_t(L)].denoiseColor;
    }
    pipeline_.setParams(nDenoiseField_, &field, sizeof field);

    // The model through the matrix (§2.3): Y = wᵀM x, so with k = Mᵀw the
    // capture chain's two sums carry over with k in place of w.
    constexpr float w[3] = {0.2627f, 0.6780f, 0.0593f};
    float k[3] = {};
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) k[c] += w[r] * camToWorking_[std::size_t(r * 3 + c)];
    const auto m = denoiseMultipliers(adj);
    float sumA = 0.0f, sumB = 0.0f;
    for (int c = 0; c < 3; ++c) {
        sumA += k[c] * k[c] * m[std::size_t(c)];
        sumB += k[c] * k[c] * m[std::size_t(c)] * m[std::size_t(c)];
    }

    for (int j = 0; j < kDenoiseScales; ++j) {
        params::AtrousShrink shrink{};
        shrink.size[0] = width_;
        shrink.size[1] = height_;
        shrink.noiseA = noise_.a * sumA;
        shrink.noiseB = noise_.b * sumB;
        shrink.scaleNorm = kDenoiseScaleNorm[j];
        shrink.detail = adj.denoiseDetail;
        shrink.step = 1 << j;
        // The strengths come from the field; the two scalars are unread.
        shrink.useField = 1;
        pipeline_.setParams(nLocalShrink_[j], &shrink, sizeof shrink);
    }
}

}  // namespace orion::pipe
