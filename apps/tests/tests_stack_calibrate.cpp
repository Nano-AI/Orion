/*  tests_stack_calibrate — calibration frames (research/stacking.md §7): the
 *  master dark keeps a hot pixel and drops the noise, the flat's gains have
 *  mean 1 per channel and undo vignetting and tint, the mosaic round-trips,
 *  and through the facade a dark and a flat remove a fixed hot pixel and a
 *  vignette from a tripod stack. testStackCalibratedRaws takes the mosaic path
 *  on real raws when ORION_SAMPLES is set, and says it skipped otherwise.
 */

#include "harness.h"
#include "orion/orion.h"
#include "stack/Calibrate.h"
#include "stack_fixture.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>
#include <utility>

namespace {

using orion::stack::calibrate;
using orion::stack::flatGain;
using orion::stack::medianOf;

/// Standard deviation of v over the samples `use` accepts.
template <class Use> double sigmaOf(const std::vector<float>& v, Use use) {
    double s = 0, s2 = 0, n = 0;
    for (std::size_t i = 0; i < v.size(); ++i)
        if (use(i)) { s += v[i]; s2 += double(v[i]) * v[i]; n += 1; }
    return std::sqrt(std::max(0.0, s2 / n - (s / n) * (s / n)));
}

}  // namespace

void testStackMasterDark() {
    section("stack calibrate: the master dark keeps the hot pixel, drops the noise");
    constexpr std::size_t N = 64 * 48, kHot = 10 * 64 + 20;
    constexpr float kPedestal = 0.01f, kNoise = 0.005f, kHotAmp = 0.5f, kScene = 0.2f;
    std::mt19937 rng(7);
    std::normal_distribution<float> g(0.0f, kNoise);
    const auto frame = [&](float scene) {
        std::vector<float> v(N);
        for (auto& s : v) s = scene + kPedestal + g(rng);
        v[kHot] += kHotAmp;
        return v;
    };
    std::vector<std::vector<float>> darks;
    for (int i = 0; i < 5; ++i) darks.push_back(frame(0.0f));
    const auto master = medianOf(darks);
    CHECK(master.size() == N);
    if (master.size() != N) return;
    const auto notHot = [&](std::size_t i) { return i != kHot; };
    const double sMaster = sigmaOf(master, notHot), sDark = sigmaOf(darks[0], notHot);
    std::printf("    master hot %.4f (want %.3f); noise %.5f vs one dark's %.5f\n", master[kHot],
                kPedestal + kHotAmp, sMaster, sDark);
    CHECK(std::abs(master[kHot] - (kPedestal + kHotAmp)) < 4 * kNoise);
    CHECK(sMaster < 0.8 * sDark);

    auto light = frame(kScene);
    calibrate(light.data(), light.size(), master, {});
    const double s = sigmaOf(light, notHot);
    std::printf("    calibrated light at the hot pixel %.4f (scene %.2f, 3 sigma %.4f)\n", light[kHot], kScene, 3 * s);
    CHECK(std::abs(light[kHot] - kScene) < 3 * s);

    // An even count takes the mean of the middle two.
    const auto even = medianOf({{1.0f}, {4.0f}, {2.0f}, {3.0f}});
    CHECK(even.size() == 1 && even[0] == 2.5f);
}

void testStackFlatGain() {
    section("stack calibrate: flat gains have mean 1 per channel and undo vignetting and tint");
    constexpr int W = 80, H = 60;
    constexpr float kDark = 0.05f;
    const float tint[3] = {0.9f, 1.0f, 0.6f};
    const auto vig = [&](int x, int y) {
        const float dx = float(x) - W / 2.0f, dy = float(y) - H / 2.0f;
        return 1.0f - 0.4f * (dx * dx + dy * dy) / (W * W / 4.0f + H * H / 4.0f);
    };
    std::vector<float> flat(std::size_t(W) * H * 3), dark(flat.size(), kDark), light(flat.size());
    std::vector<std::uint8_t> channel(flat.size());
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            for (int c = 0; c < 3; ++c) {
                const std::size_t i = (std::size_t(y) * W + std::size_t(x)) * 3 + std::size_t(c);
                channel[i] = std::uint8_t(c);
                flat[i] = 0.5f * tint[c] * vig(x, y) + kDark;
                light[i] = 0.3f * tint[c] * vig(x, y) + kDark;
            }
    constexpr std::size_t kDead = (30 * W + 40) * 3 + 1;   // a dead sample in the flat: no signal over the dark
    flat[kDead] = kDark;
    constexpr std::size_t kWeak = (10 * W + 10) * 3;   // barely over the dark: a dust core, not a gain to divide by
    flat[kWeak] = kDark + 0.001f;

    const auto gain = flatGain(flat, dark, channel);
    CHECK(gain.size() == flat.size());
    if (gain.size() != flat.size()) return;
    report(gain[kDead] == 1.0f, "a sample with no signal gets gain 1");
    report(gain[kWeak] == 1.0f, "a sample barely over the dark gets gain 1, not a tiny divisor");
    calibrate(light.data(), light.size(), dark, gain);
    for (int c = 0; c < 3; ++c) {
        double sum = 0, n = 0;
        float lo = 1e9f, hi = -1e9f;
        for (std::size_t i = std::size_t(c); i < gain.size(); i += 3) {
            if (i == kDead || i == kWeak) continue;
            sum += gain[i]; n += 1;
            lo = std::min(lo, light[i]);
            hi = std::max(hi, light[i]);
        }
        std::printf("    channel %d: mean gain %.5f, calibrated light %.4f..%.4f (spread %.3f%%)\n", c, sum / n, lo, hi,
                    100.0 * (hi - lo) / lo);
        CHECK(std::abs(sum / n - 1.0) < 1e-4);
        CHECK((hi - lo) / lo < 0.01f);
    }

    // A clipped sample stays clipped through the dark and a flat that would pull it down (gain > 1 here).
    std::vector<float> clipped = {1.0f, 0.5f};
    calibrate(clipped.data(), 2, {0.05f, 0.05f}, {1.2f, 1.2f}, 1.0f);
    report(clipped[0] == 1.0f && std::abs(clipped[1] - 0.375f) < 1e-6f, "a clipped linear sample stays at the clip");
}

void testStackBayerRoundTrip() {
    section("stack calibrate: the mosaic round-trips through float");
    orion::raw::BayerImage img;
    img.width = 16;
    img.height = 12;
    img.filters = 0x94949494;   // RGGB
    img.black = {512, 514, 510, 516};
    img.white = 15000;
    std::mt19937 rng(3);
    img.samples.resize(img.pixelCount());
    for (std::uint32_t y = 0; y < img.height; ++y)
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto b = img.black[std::size_t(img.channelAt(x, y))];
            img.samples[std::size_t(y) * img.width + x] = std::uint16_t(std::uniform_int_distribution<int>(b, img.white)(rng));
        }
    const auto m = orion::stack::mosaicOf(img);
    const auto ch = orion::stack::channelsOf(img);
    CHECK(m.size() == img.pixelCount() && ch.size() == img.pixelCount());
    if (m.size() != img.pixelCount() || ch.size() != img.pixelCount()) return;
    bool exact = true;
    for (std::uint32_t y = 0; y < img.height; ++y)
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const std::size_t i = std::size_t(y) * img.width + x;
            exact &= ch[i] == std::uint8_t(img.channelAt(x, y)) &&
                     m[i] == float(img.samples[i]) - float(img.black[ch[i]]);
        }
    report(exact, "mosaicOf is the sample minus its channel's black");
    auto back = img;
    orion::stack::storeMosaic(back, m);
    report(back.samples == img.samples, "storeMosaic(mosaicOf(img)) is the identity");

    auto edge = m;
    edge[0] = -100.0f;   // R: below black
    edge[1] = 1e6f;      // G: over white
    edge[2] = 3.6f;      // R: rounds
    back.samples[0] = back.samples[1] = back.samples[2] = 1000;   // unclipped, whatever the seed drew
    back.samples[3] = img.white;   // G: clipped in the light
    edge[3] = 9000.0f;             // what a dark and a flat made of it
    orion::stack::storeMosaic(back, edge);
    CHECK(back.samples[0] == 512 && back.samples[1] == 15000 && back.samples[2] == 516);
    report(back.samples[3] == img.white, "a clipped mosaic sample stays at white");
}

void testStackFacadeCalibrated() {
    section("stack facade: darks and flats remove a fixed hot pixel and a vignette");
    using stackfix::kTH;
    using stackfix::kTW;
    constexpr std::uint32_t kHx = 101, kHy = 77;
    constexpr float kPedestal = 0.02f, kHotAmp = 0.6f;
    const auto vig = [](std::uint32_t x, std::uint32_t y) {
        const float dx = float(x) - kTW / 2.0f, dy = float(y) - kTH / 2.0f;
        return 1.0f - 0.4f * (dx * dx + dy * dy) / (kTW * kTW / 4.0f + kTH * kTH / 4.0f);
    };
    // What the sensor adds to any frame, lit or not: the pedestal and the hot pixel.
    const auto bias = [&](std::vector<float> rgb) {
        for (auto& v : rgb) v += kPedestal;
        for (int c = 0; c < 3; ++c) rgb[(std::size_t(kHy) * kTW + kHx) * 3 + std::size_t(c)] += kHotAmp;
        return rgb;
    };
    // A lit frame: the scene (or a flat field) through the vignette, then the bias.
    const auto sensor = [&](std::vector<float> rgb) {
        for (std::uint32_t y = 0; y < kTH; ++y)
            for (std::uint32_t x = 0; x < kTW; ++x)
                for (int c = 0; c < 3; ++c) rgb[(std::size_t(y) * kTW + x) * 3 + std::size_t(c)] *= vig(x, y);
        return bias(std::move(rgb));
    };
    std::vector<std::string> lights, darks, flats;
    const std::string plain = "/tmp/orion-stack-cal-plain.dng", out = "/tmp/orion-stack-cal.dng";
    std::remove(plain.c_str());
    std::remove(out.c_str());
    OrionEngine* engine = nullptr;
    try {
        for (int i = 0; i < 4; ++i) {
            lights.push_back("/tmp/orion-stack-cal-l" + std::to_string(i) + ".dng");
            stackfix::writeFrame(lights.back(), sensor(stackfix::tiles(0, 0, 0.005f, 70u + unsigned(i))), kTW, kTH);
        }
        std::mt19937 rng(11);
        std::normal_distribution<float> g(0.0f, 0.005f);
        for (int i = 0; i < 3; ++i) {
            std::vector<float> d(std::size_t(kTW) * kTH * 3), f(d.size());
            for (auto& v : d) v = g(rng);
            for (auto& v : f) v = 0.3f + g(rng);   // under 1 with the hot pixel on top: the writer clips at 1
            darks.push_back("/tmp/orion-stack-cal-d" + std::to_string(i) + ".dng");
            flats.push_back("/tmp/orion-stack-cal-f" + std::to_string(i) + ".dng");
            stackfix::writeFrame(darks.back(), bias(d), kTW, kTH);
            stackfix::writeFrame(flats.back(), sensor(f), kTW, kTH);
        }
        if (orion_engine_create(&engine) != ORION_OK) throw std::runtime_error(gpuProblem());
        std::vector<const char*> l, d, f;
        for (const auto& s : lights) l.push_back(s.c_str());
        for (const auto& s : darks) d.push_back(s.c_str());
        for (const auto& s : flats) f.push_back(s.c_str());
        const OrionStatus st0 = orion_engine_stack(engine, l.data(), 4, 0, 0, -1, 0, nullptr, 0, nullptr, 0, nullptr, 0, 0,
                                                   plain.c_str());
        report(st0 == ORION_OK, "the uncalibrated stack succeeds", st0 == ORION_OK ? "" : orion_last_error(engine));
        const OrionStatus st = orion_engine_stack(engine, l.data(), 4, 0, 0, -1, 0, d.data(), 3, f.data(), 3, nullptr, 0, 0,
                                                  out.c_str());
        report(st == ORION_OK, "the calibrated stack succeeds", st == ORION_OK ? "" : orion_last_error(engine));

        std::uint32_t w = 0, h = 0;
        const auto raw = stackfix::reopen(plain, w, h);
        const auto cal = stackfix::reopen(out, w, h);
        CHECK(w == kTW && h == kTH);
        const auto at = [&](const std::vector<float>& rgb, std::uint32_t x, std::uint32_t y) {
            return rgb[(std::size_t(y) * kTW + x) * 3 + 1];
        };
        // Output over scene, averaged inside the tiles of a 40 px box: the vignette is the corner against the centre.
        const auto ratio = [&](const std::vector<float>& rgb, std::uint32_t x0, std::uint32_t y0) {
            double s = 0, n = 0;
            for (std::uint32_t y = y0; y < y0 + 40; ++y)
                for (std::uint32_t x = x0; x < x0 + 40; ++x) {
                    if (x % 24 < 3 || x % 24 > 20 || y % 24 < 3 || y % 24 > 20) continue;
                    s += at(rgb, x, y) / stackfix::tile(float(x), float(y));
                    n += 1;
                }
            return s / n;
        };
        const double vRaw = ratio(raw, 10, 10) / ratio(raw, 220, 160), vCal = ratio(cal, 10, 10) / ratio(cal, 220, 160);
        const float scene = stackfix::tile(float(kHx), float(kHy)) * vig(kHx, kHy) + kPedestal;
        const double want = stackfix::tile(float(kHx), float(kHy)) * ratio(cal, 220, 160);   // the scene times the flat's mean
        std::printf("    hot pixel: uncalibrated %.4f (scene %.4f), calibrated %.4f (want %.4f +- 0.02)\n",
                    at(raw, kHx, kHy), scene, at(cal, kHx, kHy), want);
        std::printf("    corner/centre: uncalibrated %.3f, calibrated %.3f (need 0.97..1.03)\n", vRaw, vCal);
        CHECK(at(raw, kHx, kHy) > scene + 0.3f);   // the positive control: a tripod stack keeps a fixed hot pixel
        CHECK(std::abs(at(cal, kHx, kHy) - want) < 0.02);
        CHECK(vRaw < 0.9);
        CHECK(vCal > 0.97 && vCal < 1.03);
    } catch (const std::exception& e) {
        report(false, "calibrated stack fixture", e.what());
    }
    if (engine) orion_engine_destroy(engine);
    for (const auto* v : {&lights, &darks, &flats})
        for (const auto& s : *v) std::remove(s.c_str());
    std::remove(plain.c_str());
    std::remove(out.c_str());
}

/// The mosaic path on real raws, when ORION_SAMPLES names a folder holding _PIC8095/8148/8220.ARW (one camera):
/// two identical lights (alignment is identity), plain, with a dark, then with a dark and a flat. The samples are
/// not calibration frames, so this asserts only what any dark must do (lower the result) and that a flat leaves it finite.
void testStackCalibratedRaws() {
    section("stack facade: calibration on real mosaics (ORION_SAMPLES)");
    const char* dir = std::getenv("ORION_SAMPLES");
    if (!dir) { std::printf("    skipped: ORION_SAMPLES is not set\n"); return; }
    const std::string a = std::string(dir) + "/_PIC8095.ARW", b = std::string(dir) + "/_PIC8148.ARW",
                      c = std::string(dir) + "/_PIC8220.ARW";
    const std::string outs[3] = {"/tmp/orion-stack-raw0.dng", "/tmp/orion-stack-raw1.dng", "/tmp/orion-stack-raw2.dng"};
    for (const auto& o : outs) std::remove(o.c_str());
    OrionEngine* engine = nullptr;
    try {
        if (orion_engine_create(&engine) != ORION_OK) throw std::runtime_error(gpuProblem());
        const char* lights[2] = {a.c_str(), a.c_str()};
        const char* dark[1] = {b.c_str()};
        const char* flat[1] = {c.c_str()};
        double mean[3] = {};
        bool finite = true;
        for (int k = 0; k < 3; ++k) {
            const OrionStatus st = orion_engine_stack(engine, lights, 2, 0, 0, -1, 0, k ? dark : nullptr, k ? 1 : 0,
                                                      k == 2 ? flat : nullptr, k == 2 ? 1 : 0, nullptr, 0, 0, outs[k].c_str());
            report(st == ORION_OK, "raw stack " + std::to_string(k) + " succeeds", st == ORION_OK ? "" : orion_last_error(engine));
            std::uint32_t w = 0, h = 0;
            const auto rgb = stackfix::reopen(outs[k], w, h);
            for (const float v : rgb) { mean[k] += v; finite &= std::isfinite(v); }
            mean[k] /= double(rgb.size());
        }
        std::printf("    mean: plain %.5f, dark %.5f, dark+flat %.5f\n", mean[0], mean[1], mean[2]);
        CHECK(mean[1] < mean[0] && mean[1] > 0.0);
        CHECK(finite && mean[2] > 0.0);
    } catch (const std::exception& e) {
        report(false, "raw calibration fixture", e.what());
    }
    if (engine) orion_engine_destroy(engine);
    for (const auto& o : outs) std::remove(o.c_str());
}
