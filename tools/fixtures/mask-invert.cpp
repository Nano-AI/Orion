// Regenerate from the repository root (no GPU or external libraries needed):
// c++ -std=c++20 -Iengine/src tools/fixtures/mask-invert.cpp \
//   engine/src/util/DngWriter.cpp -o /tmp/orion-mask-fixture
// /tmp/orion-mask-fixture
// A uniform, non-dark 64x64 frame makes both mask regions measurable even
// when the local sample RAW is a night photograph. Reuse Orion's DNG writer.
#include "util/DngWriter.h"
#include <vector>

int main() {
    constexpr unsigned size = 64;
    std::vector<float> rgb(size * size * 3, 0.18f);
    orion::util::DngLinearImage image;
    image.width = image.height = size;
    image.rgb = rgb.data();
    image.camera = "SONY ILCE-7RM3";
    // Same color metadata as apps/tests/tests_dng.cpp.
    image.xyzToCam = {0.6640f, -0.1847f, -0.0503f,
                      -0.5238f, 1.3465f, 0.1916f,
                      -0.0879f, 0.1636f, 0.6271f};
    image.asShotNeutral = {1, 1, 1};
    orion::util::writeDngLinear("tools/fixtures/mask-invert.dng", image);
}
