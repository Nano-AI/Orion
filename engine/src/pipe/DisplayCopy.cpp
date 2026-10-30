// The canvas's copy of a render (#386). See DisplayCopy.h.

#include "pipe/DisplayCopy.h"

#include <algorithm>

namespace orion::pipe {

namespace {
/// display_reduce.slang's parameter block, byte for byte.
struct DisplayReduceParams {
    std::uint32_t size[2];
    std::uint32_t valid[2];
    std::uint32_t block[2];
};
static_assert(sizeof(DisplayReduceParams) == 24);

constexpr int kMaxLevel = 12;   // SharedPicture.Level's limit: a 4096-pixel block
}  // namespace

DisplayCopy::DisplayCopy(gpu::Device& device, const std::string& shaderDir)
    : device_(device),
      library_(gpu::Library::createFromFile(device, shaderDir + "/displayReduce.metallib")),
      kernel_(gpu::Kernel::create(device, *library_, "displayReduce")),
      readback_(device) {}

void DisplayCopy::sizeAt(std::uint32_t width, std::uint32_t height, int& level,
                         std::uint32_t& outWidth, std::uint32_t& outHeight) noexcept {
    const std::uint32_t bw = (width + 1) / 2, bh = (height + 1) / 2;
    if (level <= 0 || bw < 2 || bh < 2) {
        level = 0;
        outWidth = width;
        outHeight = height;
        return;
    }
    int mips = 1;   // the chain's mip levels, as Metal counts them
    while ((std::max(bw, bh) >> mips) > 0) ++mips;
    level = std::min({level, mips, kMaxLevel});
    outWidth  = std::max<std::uint32_t>(1, bw >> (level - 1));
    outHeight = std::max<std::uint32_t>(1, bh >> (level - 1));
}

const gpu::Texture& DisplayCopy::reduce(const gpu::Texture& src, std::uint32_t width,
                                        std::uint32_t height, int level, bool wait,
                                        std::uint32_t& outWidth, std::uint32_t& outHeight) {
    sizeAt(width, height, level, outWidth, outHeight);
    if (level == 0 && src.format() == gpu::PixelFormat::RGBA8Unorm) return src;
    if (!target_ || target_->width() != outWidth || target_->height() != outHeight) {
        target_ = gpu::Texture::create(device_, outWidth, outHeight, gpu::PixelFormat::RGBA8Unorm);
    }
    const std::uint32_t block = 1u << level;
    const DisplayReduceParams p{{outWidth, outHeight}, {width, height}, {block, block}};
    // Made only when there is work: Metal will not release an encoder never ended.
    gpu::CommandBuffer cb(device_);
    cb.dispatch(*kernel_, {&src, target_.get()}, &p, sizeof p, outWidth, outHeight);
    if (wait) cb.commitAndWait();
    else cb.commit();   // the readback is queued behind it
    return *target_;
}

std::uint64_t DisplayCopy::begin(const gpu::Texture& src, std::uint32_t width, std::uint32_t height,
                                 int level, std::uint32_t& outWidth, std::uint32_t& outHeight) {
    const gpu::Texture& out = reduce(src, width, height, level, false, outWidth, outHeight);
    return readback_.begin(out, outWidth, outHeight);
}

void DisplayCopy::read(const gpu::Texture& src, std::uint32_t width, std::uint32_t height,
                       std::uint8_t* dst) {
    std::uint32_t w = 0, h = 0;
    const gpu::Texture& out = reduce(src, width, height, 0, true, w, h);
    out.download(dst, std::size_t(w) * 4, w, h);
}

}  // namespace orion::pipe
