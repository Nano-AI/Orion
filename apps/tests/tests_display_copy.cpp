// The display copy (#386): a render reduced and converted on the GPU, read back
// without waiting, the newest copy handed out. One fixture: a small odd-sized
// picture, wide (RGBA16F, values past 0..1 included) and 8-bit, against the CPU
// arithmetic the GPU replaced. Under ORION_GPU=dawn this is the staging ring
// Windows and Linux run; on Metal the same ring copies synchronously.

#include "harness.h"

#include "orion/orion.h"
#include "pipe/DisplayCopy.h"

#include <cstring>

#ifdef _WIN32
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif

namespace {
constexpr std::uint32_t kW = 37, kH = 23;   // odd both ways: the halving's clamped edge

std::uint8_t to8(float v) { return std::uint8_t(v <= 0 ? 0 : v >= 1 ? 255 : int(v * 255.0f + 0.5f)); }

int worst(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) return 256;
    int d = 0;
    for (std::size_t i = 0; i < a.size(); ++i) d = std::max(d, std::abs(int(a[i]) - int(b[i])));
    return d;
}
#ifdef _WIN32
/// The other side of DisplayCopy::share, as ANGLE takes it: the NT handle opened
/// on a D3D11 device of its own (whichever adapter will open it, WARP in CI),
/// copied out while holding the keyed mutex with key 0, then mapped.
bool readShared(void* handle, std::vector<std::uint8_t>& out, std::uint32_t& w, std::uint32_t& h,
                std::string& why) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) { why = "no DXGI factory"; return false; }
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11Device1> device1;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D11Texture2D> shared, staging;
        ComPtr<IDXGIKeyedMutex> mutex;
        if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                     D3D11_SDK_VERSION, &device, nullptr, &context)) ||
            FAILED(device.As(&device1)) || FAILED(device1->OpenSharedResource1(handle, IID_PPV_ARGS(&shared))))
            continue;   // another adapter's handle
        D3D11_TEXTURE2D_DESC d{};
        shared->GetDesc(&d);
        w = d.Width;
        h = d.Height;
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d.MiscFlags = 0;
        if (FAILED(device->CreateTexture2D(&d, nullptr, &staging)) || FAILED(shared.As(&mutex))) {
            why = "no staging texture or no keyed mutex";
            return false;
        }
        if (mutex->AcquireSync(0, 5000) != S_OK) { why = "the keyed mutex was not released in 5 s"; return false; }
        context->CopyResource(staging.Get(), shared.Get());
        mutex->ReleaseSync(0);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) { why = "map failed"; return false; }
        out.resize(std::size_t(w) * h * 4);
        for (std::uint32_t y = 0; y < h; ++y)
            std::memcpy(out.data() + std::size_t(y) * w * 4, static_cast<const std::uint8_t*>(m.pData) + y * m.RowPitch, w * 4);
        context->Unmap(staging.Get(), 0);
        return true;
    }
    why = "no D3D11 adapter opened the handle";
    return false;
}
#endif
}  // namespace

void testDisplayCopyGpu() {
    section("display copy: reduced on the GPU, read back without waiting (GPU)");
    namespace gpu = orion::gpu;
    std::unique_ptr<gpu::Device> device;
    try {
        device = gpu::Device::create();
    } catch (const std::exception& e) {
        report(false, "GPU device available", e.what());
        return;
    }
    try {
        orion::pipe::DisplayCopy copy(*device, ORION_SHADER_DIR);

        // ── A wide picture to 8 bits, on the GPU: what the CPU loop did ─────
        std::vector<__fp16> wide(std::size_t(kW) * kH * 4);
        std::vector<std::uint8_t> want(wide.size());
        for (std::size_t i = 0; i < wide.size(); ++i) {
            const float v = -0.2f + 1.4f * float((i * 7919) % 1000) / 999.0f;   // under 0 and over 1
            wide[i] = static_cast<__fp16>(v);
            want[i] = to8(float(wide[i]));
        }
        auto src16 = gpu::Texture::create(*device, kW + 5, kH + 5, gpu::PixelFormat::RGBA16Float);
        // Past the picture, in the rest of the texture: what a read past its edge would average in.
        std::vector<__fp16> padded(std::size_t(kW + 5) * (kH + 5) * 4, static_cast<__fp16>(9.0f));
        for (std::uint32_t y = 0; y < kH; ++y)
            std::copy_n(wide.begin() + std::size_t(y) * kW * 4, kW * 4, padded.begin() + std::size_t(y) * (kW + 5) * 4);
        src16->upload(padded.data(), std::size_t(kW + 5) * 8);
        std::vector<std::uint8_t> got(want.size());
        copy.read(*src16, kW, kH, got.data());
        report(worst(got, want) <= 1, "a wide picture reads back as the CPU converted it, within one code",
               "worst " + std::to_string(worst(got, want)));

        // ── The same, asynchronously, at level 0 ────────────────────────────
        std::uint32_t w = 0, h = 0;
        const auto ticket = copy.begin(*src16, kW, kH, 0, w, h);
        std::uint32_t lw = 0, lh = 0;
        std::vector<std::uint8_t> async(want.size());
        const bool landed = copy.readback().newest(true, lw, lh) == ticket && lw == kW && lh == kH &&
                            copy.readback().take(async.data(), kW * 4);
        report(landed && async == got, "level 0 without waiting is the waited read, byte for byte");

        // ── Level 2: present.mm's size, the mean of a 4x4 block ─────────────
        copy.begin(*src16, kW, kH, 2, w, h);
        int level = 2;
        std::uint32_t sw = 0, sh = 0;
        orion::pipe::DisplayCopy::sizeAt(kW, kH, level, sw, sh);
        std::vector<std::uint8_t> small(std::size_t(w) * h * 4), box(small.size());
        for (std::uint32_t y = 0; y < h; ++y)
            for (std::uint32_t x = 0; x < w; ++x)
                for (int c = 0; c < 4; ++c) {
                    float sum = 0;
                    for (std::uint32_t j = 0; j < 4; ++j)
                        for (std::uint32_t i = 0; i < 4; ++i)
                            sum += float(wide[(std::size_t(std::min(y * 4 + j, kH - 1)) * kW +
                                               std::min(x * 4 + i, kW - 1)) * 4 + c]);
                    box[(std::size_t(y) * w + x) * 4 + c] = to8(sum / 16.0f);
                }
        const bool took = copy.readback().newest(true, lw, lh) != 0 && copy.readback().take(small.data(), w * 4);
        report(w == (kW + 1) / 2 / 2 && h == (kH + 1) / 2 / 2 && sw == w && sh == h,
               "level 2 is present.mm's size: halve rounding up, then down",
               std::to_string(w) + "x" + std::to_string(h));
        report(took && worst(small, box) <= 1, "and each pixel the mean of its 4x4 block, none from past the picture",
               "worst " + std::to_string(worst(small, box)));

        // ── The newest wins; older copies are dropped, never queued ─────────
        auto src8 = gpu::Texture::create(*device, kW, kH, gpu::PixelFormat::RGBA8Unorm);
        std::uint64_t last = 0;
        for (int k = 1; k <= 5; ++k) {   // five begun, three buffers: the ring reuses without a take
            std::vector<std::uint8_t> flat(std::size_t(kW) * kH * 4, std::uint8_t(40 * k));
            src8->upload(flat.data(), kW * 4);
            last = copy.begin(*src8, kW, kH, 0, w, h);
        }
        std::vector<std::uint8_t> newest(std::size_t(kW) * kH * 4);
        const auto named = copy.readback().newest(true, lw, lh);
        const bool fifth = named == last && copy.readback().take(newest.data(), kW * 4) &&
                           newest[0] == 200 && newest.back() == 200;
        report(fifth, "after five begun, the one handed out is the fifth",
               "ticket " + std::to_string(named) + " of " + std::to_string(last) + ", pixel " + std::to_string(newest[0]));
        report(copy.readback().newest(false, lw, lh) == 0, "and nothing older is left to hand out");

        // ── Windows: the shared D3D11 texture ANGLE imports, read as it reads it ─
        int generation = 0;
        void* handle = copy.share(*src16, kW, kH, 0, generation, w, h);
#ifdef _WIN32
        if (!handle) {
            std::printf("  NOTE  no shared D3D11 texture on this adapter (no DXGISharedHandle feature): not exercised\n");
        } else {
            std::vector<std::uint8_t> seen;
            std::string why;
            std::uint32_t dw = 0, dh = 0;
            report(readShared(handle, seen, dw, dh, why) && dw == kW && dh == kH && seen == got,
                   "D3D11 reads the shared texture as the readback, byte for byte", why);
            const int first = generation;
            void* again = copy.share(*src16, kW, kH, 2, generation, w, h);   // another size: a new texture
            report(again && generation != first && readShared(again, seen, dw, dh, why) && dw == w && dh == h &&
                       seen == small,
                   "and at level 2, a new texture of a new generation, as the readback again", why);
        }
#else
        report(handle == nullptr && generation == 0, "no shared D3D11 texture off Windows");
#endif
    } catch (const std::exception& e) {
        report(false, "display copy runs", e.what());
    }

    // ── The facade refuses what it cannot copy ──────────────────────────────
    std::uint32_t w = 0, h = 0;
    std::uint64_t t = 0;
    report(orion_engine_display_begin(nullptr, 0, 0, &w, &h, &t) == ORION_ERR_BAD_ARG &&
               orion_engine_display_take(nullptr, 0, nullptr, 0, 0, &w, &h, &t) == ORION_ERR_BAD_ARG,
           "display begin and take refuse a null engine");
    OrionEngine* e = nullptr;
    if (orion_engine_create(&e) == ORION_OK) {
        report(orion_engine_display_begin(e, 0, 0, &w, &h, &t) == ORION_ERR_BAD_ARG &&
                   orion_engine_display_begin(e, 3, 0, &w, &h, &t) == ORION_ERR_BAD_ARG &&
                   orion_engine_display_take(e, 0, nullptr, 0, 0, &w, nullptr, &t) == ORION_ERR_BAD_ARG,
               "and an engine with no render, a graph past 2, a null size");
        report(orion_engine_display_take(e, 1, nullptr, 0, 0, &w, &h, &t) == ORION_OK && t == 0,
               "a take with nothing begun is no copy, not an error", orion_last_error(e));
    }
    orion_engine_destroy(e);
}
