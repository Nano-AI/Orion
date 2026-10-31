/*  tests_stack_facade — the stacker end to end through the C facade: frames
 *  written as linear DNGs by Orion's own writer (as tests_hdr_merge.cpp does),
 *  one stacked DNG out, reopened and asserted against the truth the frames
 *  were built from. Noise mode on a textured scene (the ORB aligner needs
 *  texture), Stars on a rotating star field, Landscape on a sky that moves
 *  over a ground that does not.
 *
 *  testStackBench runs only with ORION_STACK_BENCH=1 (main.cpp then runs it
 *  alone): 20 frames at 24 MP, written under /tmp/orion-stack-bench/ and
 *  deleted at the end.
 */

#include "harness.h"
#include "orion/orion.h"
#include "stack/Stacker.h"
#include "stack_fixture.h"
#include "util/DngWriter.h"
#include "util/Half.h"

#ifndef _WIN32
#include <sys/resource.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <random>
#include <thread>

namespace {

namespace fs = std::filesystem;
namespace persp = orion::pipe::persp;
using orion::stack::Star;

using stackfix::kTH;
using stackfix::kTW;
using stackfix::reopen;
using stackfix::tile;
using stackfix::tiles;
using stackfix::writeFrame;

OrionStatus stackFiles(OrionEngine* e, const std::vector<std::string>& files, int ref, int mode,
                       const std::string& out, const float* matte = nullptr, int mw = 0, int mh = 0) {
    std::vector<const char*> p;
    for (const auto& f : files) p.push_back(f.c_str());
    return orion_engine_stack(e, p.data(), int32_t(p.size()), ref, mode, -1, 0, nullptr, 0, nullptr, 0,
                              matte, mw, mh, out.c_str(), 0);
}

void removeAll(const std::vector<std::string>& files) {
    for (const auto& f : files) std::remove(f.c_str());
}

/// Six 320x240 star frames, shifted a few pixels apart, for the refusals.
std::vector<std::string> starFiles(const std::string& prefix, int n) {
    std::vector<std::string> files;
    for (int i = 0; i < n; ++i) {
        const auto m = stackfix::rotation(0.0f, 0.0f, 0.0f, float(2 * i), float(i));
        files.push_back(prefix + std::to_string(i) + ".dng");
        writeFrame(files.back(), stackfix::starField(23, 60, 320, 240, m, 0.005f, 0.05f, 40u + unsigned(i)).rgb, 320, 240);
    }
    return files;
}

}  // namespace

void testStackFacadeNoise() {
    section("stack facade: noise mode, 8 shifted frames, noise falls by sqrt 8");
    const float dx[8] = {0, 3, -2, 5, -4, 1, 4, -3}, dy[8] = {0, -2, 4, 1, -3, 5, -1, 2};
    std::vector<std::string> files;
    const std::string out = "/tmp/orion-stack-noise.dng";
    std::remove(out.c_str());
    OrionEngine* engine = nullptr;
    try {
        for (int i = 0; i < 8; ++i) {
            files.push_back("/tmp/orion-stack-n" + std::to_string(i) + ".dng");
            writeFrame(files.back(), tiles(dx[i], dy[i], 0.01f, 60u + unsigned(i)), kTW, kTH);
        }
        if (orion_engine_create(&engine) != ORION_OK) throw std::runtime_error(gpuProblem());
        float progress = -1.0f;
        CHECK(orion_engine_stack_progress(engine, &progress) == ORION_OK && progress == 0.0f);
        const OrionStatus st = stackFiles(engine, files, 0, 0, out);
        report(st == ORION_OK, "noise stack succeeds through the facade", st == ORION_OK ? "" : orion_last_error(engine));
        CHECK(orion_engine_stack_progress(engine, &progress) == ORION_OK && progress == 1.0f);

        // Residual against the known scene, inside each tile (3 px from its edges) and 8 px from the border.
        std::uint32_t w = 0, h = 0, w0 = 0, h0 = 0;
        const auto stacked = reopen(out, w, h);
        const auto single = reopen(files[0], w0, h0);
        CHECK(w == kTW && h == kTH);
        auto sigma = [&](const std::vector<float>& rgb) {
            double s = 0, s2 = 0, n = 0;
            for (std::uint32_t y = 8; y + 8 < kTH; ++y)
                for (std::uint32_t x = 8; x + 8 < kTW; ++x) {
                    if (x % 24 < 3 || x % 24 > 20 || y % 24 < 3 || y % 24 > 20) continue;
                    const double r = rgb[(std::size_t(y) * kTW + x) * 3 + 1] - tile(float(x), float(y));
                    s += r; s2 += r * r; n += 1;
                }
            return std::sqrt(s2 / n - (s / n) * (s / n));
        };
        // And over every pixel, tile edges included: a frame warped to the wrong place shows there.
        double r2 = 0, rn = 0;
        for (std::uint32_t y = 8; y + 8 < kTH; ++y)
            for (std::uint32_t x = 8; x + 8 < kTW; ++x) {
                const double r = stacked[(std::size_t(y) * kTW + x) * 3 + 1] - tile(float(x), float(y));
                r2 += r * r; rn += 1;
            }
        const double rms = std::sqrt(r2 / rn);
        std::printf("    RMS against the scene, edges included: %.5f (need <= 0.006)\n", rms);
        CHECK(rms <= 0.006);
        const double ratio = sigma(single) / sigma(stacked);
        std::printf("    sigma(single)/sigma(stack) = %.3f (sqrt 8 = 2.828; need >= %.3f)\n", ratio, 0.8 * std::sqrt(8.0));
        CHECK(ratio >= 0.8 * std::sqrt(8.0));
    } catch (const std::exception& e) {
        report(false, "noise stack fixture", e.what());
    }
    if (engine) orion_engine_destroy(engine);
    removeAll(files);
    std::remove(out.c_str());
}

void testStackFacadeStars() {
    section("stack facade: stars mode, 8 frames rotating 0.2 deg about an off-frame point");
    constexpr std::uint32_t W = 640, H = 480;
    std::vector<std::string> files;
    const std::string out = "/tmp/orion-stack-stars.dng";
    std::remove(out.c_str());
    OrionEngine* engine = nullptr;
    try {
        for (int i = 0; i < 8; ++i) {
            const auto m = stackfix::rotation(0.2f * float(i), -300.0f, -200.0f, 1.5f * float(i), -float(i));
            files.push_back("/tmp/orion-stack-s" + std::to_string(i) + ".dng");
            writeFrame(files.back(), stackfix::starField(21, 150, W, H, m, 0.01f, 0.05f, 1000u + unsigned(i)).rgb, W, H);
        }
        if (orion_engine_create(&engine) != ORION_OK) throw std::runtime_error(gpuProblem());
        const OrionStatus st = stackFiles(engine, files, 0, 1, out);
        report(st == ORION_OK, "star stack succeeds through the facade", st == ORION_OK ? "" : orion_last_error(engine));

        std::uint32_t w = 0, h = 0;
        const auto stacked = reopen(out, w, h);
        const auto ref = reopen(files[0], w, h);
        const auto got = orion::stack::detectStars(stacked.data(), w, h);
        const auto want = orion::stack::detectStars(ref.data(), w, h);
        std::vector<float> err;
        for (const Star& s : got) {
            float best = 1e9f;
            for (const Star& r : want) best = std::min(best, std::hypot(s.x - r.x, s.y - r.y));
            if (best <= 2.0f) err.push_back(best);
        }
        std::sort(err.begin(), err.end());
        const float med = err.empty() ? 1e9f : err[err.size() / 2];
        std::printf("    %zu stars matched of %zu; median registration error %.3f px (need <= 0.5, >= 40 matches)\n",
                    err.size(), got.size(), med);
        CHECK(err.size() >= 40);
        CHECK(med <= 0.5f);
    } catch (const std::exception& e) {
        report(false, "star stack fixture", e.what());
    }
    if (engine) orion_engine_destroy(engine);
    removeAll(files);
    std::remove(out.c_str());
}

void testStackFacadeLandscape() {
    section("stack facade: landscape, the sky follows the stars and the ground stays put");
    constexpr std::uint32_t W = 320, H = 240, kEdge = 160;
    constexpr float kBg = 0.1f;
    std::vector<float> matte(std::size_t(W) * H, 0.0f);   // upright, flip 0: the top half is sky
    std::fill(matte.begin(), matte.begin() + std::ptrdiff_t(W * (H / 2)), 1.0f);
    std::vector<std::string> files;
    const std::string out = "/tmp/orion-stack-land.dng";
    std::remove(out.c_str());
    OrionEngine* engine = nullptr;
    try {
        std::vector<Star> refStars;
        for (int i = 0; i < 8; ++i) {
            const auto m = stackfix::rotation(0.0f, 0.0f, 0.0f, float(3 * i), 0.0f);
            auto f = stackfix::starField(5, 40, W, H, m, 0.003f, kBg, 500u + unsigned(i));
            const auto ground = stackfix::starField(5, 0, W, H, persp::identity(), 0.003f, kBg, 900u + unsigned(i)).rgb;
            for (std::size_t p = std::size_t(W) * (H / 2); p < std::size_t(W) * H; ++p)
                for (int c = 0; c < 3; ++c)
                    f.rgb[p * 3 + std::size_t(c)] = ground[p * 3 + std::size_t(c)] + (p % W >= kEdge ? 0.4f : 0.0f);
            if (i == 0) refStars = f.reference;
            files.push_back("/tmp/orion-stack-l" + std::to_string(i) + ".dng");
            writeFrame(files.back(), f.rgb, W, H);
        }
        if (orion_engine_create(&engine) != ORION_OK) throw std::runtime_error(gpuProblem());
        const OrionStatus st = stackFiles(engine, files, 0, 2, out, matte.data(), int(W), int(H));
        report(st == ORION_OK, "landscape stack succeeds through the facade", st == ORION_OK ? "" : orion_last_error(engine));

        std::uint32_t w = 0, h = 0;
        const auto stacked = reopen(out, w, h);
        const auto single = reopen(files[0], w, h);
        const auto at = [&](const std::vector<float>& rgb, std::uint32_t x, std::uint32_t y) {
            return rgb[(std::size_t(y) * W + x) * 3];
        };
        // The ground below the matte's feather (1% of 320 px, two passes: ~7 rows).
        double e = 0, e1 = 0;
        for (std::uint32_t y = H / 2 + 10; y < H; ++y) {
            e += at(stacked, kEdge, y) - at(stacked, kEdge - 1, y);
            e1 += at(single, kEdge, y) - at(single, kEdge - 1, y);
        }
        // The sky: the brighter stars, clear of the frame edge and the feather.
        std::vector<float> peaks;
        for (const Star& s : refStars) {
            if (s.flux < 0.3f || s.y > float(H / 2 - 12) || s.x < 30.0f || s.x > float(W - 30)) continue;
            const auto x = std::uint32_t(std::lround(s.x)), y = std::uint32_t(std::lround(s.y));
            peaks.push_back((at(stacked, x, y) - kBg) / (at(single, x, y) - kBg));
        }
        std::sort(peaks.begin(), peaks.end());
        const double peak = peaks.empty() ? 0.0 : peaks[peaks.size() / 2];
        std::printf("    ground edge contrast %.3f of a single frame, star peak %.3f (median of %zu)\n",
                    e / e1, peak, peaks.size());
        CHECK(e / e1 >= 0.9);
        CHECK(peaks.size() >= 3 && peak >= 0.7);
    } catch (const std::exception& e) {
        report(false, "landscape stack fixture", e.what());
    }
    if (engine) orion_engine_destroy(engine);
    removeAll(files);
    std::remove(out.c_str());
}

void testStackFacadeRefuses() {
    section("stack facade: refusals, and a frame that does not register is skipped");
    std::vector<std::string> files;
    const std::string out = "/tmp/orion-stack-refuse.dng", odd = "/tmp/orion-stack-odd.dng",
                      blank = "/tmp/orion-stack-blank.dng", other = "/tmp/orion-stack-other.dng";
    OrionEngine* engine = nullptr;
    try {
        files = starFiles("/tmp/orion-stack-r", 6);
        if (orion_engine_create(&engine) != ORION_OK) throw std::runtime_error(gpuProblem());

        CHECK(stackFiles(engine, {files[0]}, 0, 1, out) == ORION_ERR_BAD_ARG);
        CHECK(stackFiles(engine, files, 0, 3, out) == ORION_ERR_BAD_ARG);   // no mode 3

        {   // An existing output is an error, and the file is untouched.
            std::ofstream(out) << "keep";
            CHECK(stackFiles(engine, files, 0, 1, out) != ORION_OK);
            std::ifstream in(out);
            std::string s;
            in >> s;
            report(s == "keep", "an existing output is left untouched");
            std::remove(out.c_str());
        }

        CHECK(stackFiles(engine, files, 0, 2, out) != ORION_OK);
        report(std::strstr(orion_last_error(engine), "sky") != nullptr, "landscape without a matte names the sky",
               orion_last_error(engine));

        {   // Mixed sets refuse before any decode: an odd size last of six, another camera, a dark of another size.
            writeFrame(odd, stackfix::starField(23, 60, 336, 240, persp::identity(), 0.005f, 0.05f).rgb, 336, 240);
            const char* dark[1] = {odd.c_str()};
            std::vector<const char*> p;
            for (const auto& f : files) p.push_back(f.c_str());
            CHECK(orion_engine_stack(engine, p.data(), 6, 0, 1, -1, 0, dark, 1, nullptr, 0, nullptr, 0, 0, out.c_str(), 0) !=
                  ORION_OK);
            report(std::strstr(orion_last_error(engine), "dark frames do not match the lights' size") != nullptr,
                   "a dark of another size is refused", orion_last_error(engine));
            auto mixed = files;
            mixed.back() = odd;
            CHECK(stackFiles(engine, mixed, 0, 1, out) != ORION_OK);
            report(std::strstr(orion_last_error(engine), "different sizes") != nullptr,
                   "an odd-size frame names the size", orion_last_error(engine));
            writeFrame(other, stackfix::starField(23, 60, 320, 240, persp::identity(), 0.005f, 0.05f).rgb, 320, 240,
                       "NIKON Z 6");
            mixed.back() = other;
            CHECK(stackFiles(engine, mixed, 0, 1, out) != ORION_OK);
            report(std::strstr(orion_last_error(engine), "different cameras") != nullptr,
                   "another camera's frame is refused", orion_last_error(engine));
            const char* flat[1] = {other.c_str()};
            CHECK(orion_engine_stack(engine, p.data(), 6, 0, 1, -1, 0, nullptr, 0, flat, 1, nullptr, 0, 0, out.c_str(), 0) !=
                  ORION_OK);
            report(std::strstr(orion_last_error(engine), "flat frames do not match the lights' camera") != nullptr,
                   "another camera's flat is refused", orion_last_error(engine));
            // A turned camera passes the metadata check (as displayed), but this one's sensor really is 240x320.
            writeFrame(odd, stackfix::starField(23, 60, 240, 320, persp::identity(), 0.005f, 0.05f).rgb, 240, 320);
            CHECK(orion_engine_stack(engine, p.data(), 6, 0, 1, -1, 0, dark, 1, nullptr, 0, nullptr, 0, 0, out.c_str(), 0) !=
                  ORION_OK);
            report(std::strstr(orion_last_error(engine), "do not match the lights' size") != nullptr,
                   "a transposed dark is refused after decode", orion_last_error(engine));
            report(!fs::exists(out), "no output after a refusal");
        }

        // A blank frame among six star frames: skipped, not fatal.
        writeFrame(blank, stackfix::starField(23, 0, 320, 240, persp::identity(), 0.005f, 0.05f).rgb, 320, 240);
        auto device = orion::gpu::Device::create();
        orion::stack::Stacker stacker(*device, ORION_SHADER_DIR);
        orion::stack::Request rq;
        rq.paths = files;
        rq.paths.insert(rq.paths.begin() + 3, blank);
        rq.mode = orion::stack::Mode::Stars;
        rq.output = out;
        stacker.run(rq);
        std::printf("    skipped %d of %zu\n", stacker.skipped(), rq.paths.size());
        CHECK(stacker.skipped() == 1);
        CHECK(fs::exists(out));
        std::remove(out.c_str());   // the facade reports the count too
        CHECK(stackFiles(engine, rq.paths, 0, 1, out) == ORION_OK);
        int32_t skipped = -1;
        CHECK(orion_engine_stack_skipped(engine, &skipped) == ORION_OK && skipped == 1);
    } catch (const std::exception& e) {
        report(false, "refusal fixture", e.what());
    }
    if (engine) orion_engine_destroy(engine);
    removeAll(files);
    removeAll({out, odd, blank, other});
}

void testStackCancelIsAtomic() {
    section("stack: cancellation never leaves a partial file");
    const std::vector<std::string> files = {"/tmp/orion-stackc-a.dng", "/tmp/orion-stackc-b.dng"};
    const std::string out = "/tmp/orion-stackc-out.dng";
    std::remove(out.c_str());
    try {
        writeFrame(files[0], tiles(0, 0, 0.01f, 1), kTW, kTH);
        writeFrame(files[1], tiles(3, -2, 0.01f, 2), kTW, kTH);
        auto device = orion::gpu::Device::create();
        orion::stack::Stacker stacker(*device, ORION_SHADER_DIR);
        orion::stack::Request rq;
        rq.paths = files;
        rq.output = out;

        bool threw = false;
        std::thread canceller([&] { stacker.cancel(); });
        try {
            stacker.run(rq);
        } catch (const std::exception&) {
            threw = true;
        }
        canceller.join();
        std::printf("    the run %s\n", threw ? "was cancelled" : "completed before the cancel landed");
        if (threw) report(!fs::exists(out), "a cancelled stack leaves no file");
        else report(fs::exists(out) && fs::file_size(out) > 0, "a completed stack leaves a whole file");
        report(!fs::exists(out + ".part"), "and never a partial one");

        bool failed = false;
        rq.output = "/tmp/orion-no-such-dir/x.dng";
        try {
            stacker.run(rq);
        } catch (const std::exception&) {
            failed = true;
        }
        CHECK(failed);
        report(!fs::exists("/tmp/orion-no-such-dir"), "no debris on a bad destination");
    } catch (const std::exception& e) {
        report(false, "stack cancel fixture", e.what());
    }
    removeAll(files);
    std::remove(out.c_str());
}

void testStackBench() {
    section("stack bench: star frames at 6000x4000, mode 1");
    constexpr std::uint32_t W = 6000, H = 4000;
    const std::string dir = "/tmp/orion-stack-bench/";
    try {
        fs::create_directories(dir);
        std::vector<std::string> files;
        for (int i = 0; i < 20; ++i) {
            const auto m = stackfix::rotation(0.05f * float(i), -2000.0f, -1500.0f, 2.0f * float(i), -float(i));
            files.push_back(dir + "f" + std::to_string(i) + ".dng");
            writeFrame(files.back(), stackfix::starField(31, 2000, W, H, m, 0.01f, 0.05f, 3000u + unsigned(i)).rgb, W, H);
        }
        auto device = orion::gpu::Device::create();
        orion::stack::Stacker stacker(*device, ORION_SHADER_DIR);
        for (int n : {10, 20}) {
            orion::stack::Request rq;
            rq.paths.assign(files.begin(), files.begin() + n);
            rq.mode = orion::stack::Mode::Stars;
            rq.output = dir + "out" + std::to_string(n) + ".dng";
            const auto t0 = std::chrono::steady_clock::now();
            stacker.run(rq);
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("    %d frames: %.1f s (skipped %d)\n", n, s, stacker.skipped());
            CHECK(stacker.skipped() == 0);
        }
#ifndef _WIN32
        rusage ru{};
        getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
        const double mb = double(ru.ru_maxrss) / (1024.0 * 1024.0);   // bytes on macOS
#else
        const double mb = double(ru.ru_maxrss) / 1024.0;              // kilobytes on Linux
#endif
        std::printf("    peak resident set %.0f MB\n", mb);
#endif
    } catch (const std::exception& e) {
        report(false, "stack bench", e.what());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

void testStackMemory() {
    section("stack: over the memory available, refused before any decode unless forced");
    std::vector<std::string> files;
    const std::string out = "/tmp/orion-stack-memory.dng";
    std::remove(out.c_str());
    OrionEngine* engine = nullptr;
    try {
        files = starFiles("/tmp/orion-stack-m", 3);
        if (orion_engine_create(&engine) != ORION_OK) throw std::runtime_error(gpuProblem());
        std::vector<const char*> p;
        for (const auto& f : files) p.push_back(f.c_str());
        const std::vector<const char*> many(21, files[0].c_str());

        // The estimate is the formula on the first light's size: 320 x 240 x (max(12n, 4 max(d, f)) + 94).
        std::uint64_t est = 0, cal = 0, avail = 0;
        CHECK(orion_stack_estimate(p.data(), 3, 0, 0, &est) == ORION_OK);
        CHECK(orion_stack_estimate(p.data(), 3, 21, 2, &cal) == ORION_OK);
        report(est == 320ull * 240 * (12 * 3 + 94) && cal == 320ull * 240 * (4 * 21 + 94),
               "the estimate is W x H x (max(12 lights, 4 max(darks, flats)) + 94)",
               std::to_string(est) + ", " + std::to_string(cal));
        CHECK(orion_available_memory(&avail) == ORION_OK);
        std::printf("    available now: %.1f GB\n", double(avail) / 1e9);
        report(avail > 0, "this machine says how much memory is available");

        // Through the facade: enough frames that the estimate passes 1.5x what is available. All but the first
        // are missing, so any readInfo or decode would fail with another sentence.
        const std::size_t n = std::size_t(1.5 * double(avail) / (320.0 * 240 * 12)) + 10;
        std::vector<std::string> lots(n, "/tmp/orion-stack-missing.dng");
        lots[0] = files[0];
        std::vector<const char*> lp;
        for (const auto& f : lots) lp.push_back(f.c_str());
        const OrionStatus refused = orion_engine_stack(engine, lp.data(), int32_t(n), 0, 1, -1, 0, nullptr, 0, nullptr, 0,
                                                       nullptr, 0, 0, out.c_str(), 0);
        const std::string why = orion_last_error(engine);
        report(refused == ORION_ERR_MEMORY && why.find("frames need about") != std::string::npos &&
                   why.find("GB of memory") != std::string::npos && why.find("is free") != std::string::npos,
               "without force, over the memory available is ORION_ERR_MEMORY with a sentence", why);
        const OrionStatus forced = orion_engine_stack(engine, lp.data(), int32_t(n), 0, 1, -1, 0, nullptr, 0, nullptr, 0,
                                                      nullptr, 0, 0, out.c_str(), 1);
        report(forced != ORION_OK && forced != ORION_ERR_MEMORY &&
                   std::strstr(orion_last_error(engine), "orion-stack-missing") != nullptr,
               "with force it goes on, to the missing frames' own refusal", orion_last_error(engine));
        report(!fs::exists(out), "no output after either");

        // The test seam: an override answers for the OS, in orion_available_memory and in the stack's own check.
        std::uint64_t seen = 0;
        CHECK(orion_available_memory_override(1000) == ORION_OK && orion_available_memory(&seen) == ORION_OK && seen == 1000);
        const OrionStatus tiny = stackFiles(engine, files, 0, 1, out);
        orion_available_memory_override(0);
        report(tiny == ORION_ERR_MEMORY && !fs::exists(out), "1000 bytes overridden refuses three small frames, unforced",
               orion_last_error(engine));
        CHECK(orion_available_memory(&seen) == ORION_OK && seen != 1000);

        // A forced run over an injected budget writes the stack.
        auto device = orion::gpu::Device::create();
        orion::stack::Stacker stacker(*device, ORION_SHADER_DIR);
        orion::stack::Request rq;
        rq.paths = files;
        rq.mode = orion::stack::Mode::Stars;
        rq.output = out;
        rq.availableBytes = 1;
        bool threw = false;
        try {
            stacker.run(rq);
        } catch (const orion::stack::NotEnoughMemory&) {
            threw = true;
        }
        report(threw && !fs::exists(out), "1 byte available refuses the 3 frames");
        rq.force = true;
        stacker.run(rq);
        report(fs::exists(out), "forced, the same request writes the stack");
    } catch (const std::exception& e) {
        report(false, "memory fixture", e.what());
    }
    if (engine) orion_engine_destroy(engine);
    removeAll(files);
    std::remove(out.c_str());
}
