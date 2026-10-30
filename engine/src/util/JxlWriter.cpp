#include "util/JxlWriter.h"

#include <jxl/encode_cxx.h>
#include <jxl/thread_parallel_runner_cxx.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace orion::util {
namespace {

void check(JxlEncoderStatus status, const char* what) {
    if (status != JXL_ENC_SUCCESS) throw std::runtime_error(std::string("jxl: ") + what);
}

/// RGBX rows to packed RGB. libjxl takes a fourth channel only as alpha, and
/// the writer's fourth sample is padding: declaring it alpha would put an
/// opaque alpha channel in every file for nothing.
template <class T>
std::vector<T> packRgb(const JxlInput& in) {
    std::vector<T> out(std::size_t(in.width) * in.height * 3);
    T* dst = out.data();
    for (std::uint32_t y = 0; y < in.height; ++y) {
        const T* row = reinterpret_cast<const T*>(
            static_cast<const std::uint8_t*>(in.pixels) + y * in.bytesPerRow);
        for (std::uint32_t x = 0; x < in.width; ++x, dst += 3) {
            std::memcpy(dst, row + std::size_t(x) * 4, 3 * sizeof(T));
        }
    }
    return out;
}

}  // namespace

std::vector<std::uint8_t> encodeJxl(const JxlInput& in) {
    if (in.pixels == nullptr || in.width == 0 || in.height == 0 ||
        (in.bits != 8 && in.bits != 16)) {
        throw std::runtime_error("jxl: bad input");
    }
    if (in.icc.empty()) throw std::runtime_error("jxl: no ICC profile");
    const bool lossless = in.quality >= 1.0f;
    const bool boxes = !in.exif.empty() || !in.xmp.empty();

    auto enc = JxlEncoderMake(nullptr);
    auto runner = JxlThreadParallelRunnerMake(
        nullptr, JxlThreadParallelRunnerDefaultNumWorkerThreads());
    check(JxlEncoderSetParallelRunner(enc.get(), JxlThreadParallelRunner, runner.get()),
          "parallel runner");
    if (boxes) check(JxlEncoderUseBoxes(enc.get()), "use boxes");

    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);
    info.xsize = in.width;
    info.ysize = in.height;
    info.bits_per_sample = static_cast<std::uint32_t>(in.bits);
    info.num_color_channels = 3;
    // Lossless has to keep the pixels in their own space; lossy converts to
    // XYB, which is where libjxl's quality is calibrated.
    info.uses_original_profile = lossless ? JXL_TRUE : JXL_FALSE;
    check(JxlEncoderSetBasicInfo(enc.get(), &info), "basic info");

    check(JxlEncoderSetICCProfile(enc.get(), in.icc.data(), in.icc.size()), "ICC profile");

    if (!in.exif.empty()) {
        // The box opens with the offset of the TIFF header, which follows at once.
        std::vector<std::uint8_t> box(4, 0);
        box.insert(box.end(), in.exif.begin(), in.exif.end());
        check(JxlEncoderAddBox(enc.get(), "Exif", box.data(), box.size(), JXL_FALSE), "Exif box");
    }
    if (!in.xmp.empty()) {
        check(JxlEncoderAddBox(enc.get(), "xml ", in.xmp.data(), in.xmp.size(), JXL_FALSE),
              "XMP box");
    }
    if (boxes) JxlEncoderCloseBoxes(enc.get());

    JxlEncoderFrameSettings* frame = JxlEncoderFrameSettingsCreate(enc.get(), nullptr);
    if (lossless) {
        check(JxlEncoderSetFrameLossless(frame, JXL_TRUE), "lossless");
    } else {
        const float q = std::clamp(in.quality, 0.0f, 1.0f) * 100.0f;
        check(JxlEncoderSetFrameDistance(frame, JxlEncoderDistanceFromQuality(q)), "distance");
    }

    if (in.bits == 16) {
        const auto rgb = packRgb<std::uint16_t>(in);
        const JxlPixelFormat format{3, JXL_TYPE_UINT16, JXL_LITTLE_ENDIAN, 0};
        check(JxlEncoderAddImageFrame(frame, &format, rgb.data(),
                                      rgb.size() * sizeof(std::uint16_t)), "frame");
    } else {
        const auto rgb = packRgb<std::uint8_t>(in);
        const JxlPixelFormat format{3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
        check(JxlEncoderAddImageFrame(frame, &format, rgb.data(), rgb.size()), "frame");
    }
    JxlEncoderCloseInput(enc.get());

    std::vector<std::uint8_t> out(64 * 1024);
    std::uint8_t* next = out.data();
    std::size_t avail = out.size();
    for (;;) {
        const JxlEncoderStatus status = JxlEncoderProcessOutput(enc.get(), &next, &avail);
        if (status == JXL_ENC_SUCCESS) break;
        if (status != JXL_ENC_NEED_MORE_OUTPUT) throw std::runtime_error("jxl: encode failed");
        const std::size_t used = static_cast<std::size_t>(next - out.data());
        out.resize(out.size() * 2);
        next = out.data() + used;
        avail = out.size() - used;
    }
    out.resize(static_cast<std::size_t>(next - out.data()));
    return out;
}

}  // namespace orion::util
