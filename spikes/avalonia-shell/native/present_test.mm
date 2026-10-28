// Headless check of present.mm: no window, no Avalonia.   present_test RAW
// Renders the photo through the engine, presents it into the IOSurface, and
// reads both on the CPU once, here only: the surface by IOSurfaceLock, the
// render by shim_read (the CPU path the shell keeps as its fallback). This
// test plays Avalonia: it waits on the producer and signals the consumer.
//   1. level 0: the surface is the render, byte for byte, both graphs
//   2. levels 1 and 2: the surface is the render's 2x2 average, within 1 and 2
//   3. a second present waits on the GPU for the consumer: the producer stays
//      unsignalled and the surface keeps frame 1 until the consumer signals
//   4. the engine's next render waits for that pending present to read
//   5. CPU per frame: present_frame against shim_read
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurfaceObjC.h>
#import <Metal/Metal.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <mach-o/dyld.h>
#include <time.h>
#include <vector>

extern "C" {
struct Shim;
struct Present;
Shim* shim_open(const char*);
void shim_close(Shim*);
int shim_set(Shim*, const char*, int, float);
int shim_render(Shim*, int, double*);
int shim_read(Shim*, int, uint8_t*, int, int*, int*);
Present* present_create(void);
void present_destroy(Present*);
void* present_producer(Present*);
void* present_consumer(Present*);
int present_frame(Present*, Shim*, int, int, void**, int*, int*, int*, uint64_t*);
}

using Bytes = std::vector<uint8_t>;
static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) failures++;
}

static Bytes render(Shim* s, int which) {
    int w = 0, h = 0;
    shim_read(s, which, nullptr, 0, &w, &h);
    Bytes b(size_t(w) * h * 4);
    shim_read(s, which, b.data(), int(b.size()), &w, &h);
    return b;
}

static Bytes surfaceBytes(IOSurfaceRef io) {
    IOSurfaceLock(io, kIOSurfaceLockReadOnly, nullptr);
    const size_t w = IOSurfaceGetWidth(io), h = IOSurfaceGetHeight(io), stride = IOSurfaceGetBytesPerRow(io);
    Bytes b(w * h * 4);
    for (size_t y = 0; y < h; y++)
        std::memcpy(b.data() + y * w * 4, (uint8_t*)IOSurfaceGetBaseAddress(io) + y * stride, w * 4);
    IOSurfaceUnlock(io, kIOSurfaceLockReadOnly, nullptr);
    return b;
}

// The kernel's 2x2 average, clamped to the picture: (w+1)/2 by (h+1)/2.
static Bytes halve(const Bytes& src, int& w, int& h) {
    const int ow = (w + 1) / 2, oh = (h + 1) / 2;
    Bytes out(size_t(ow) * oh * 4);
    auto at = [&](int x, int y, int k) { return float(src[(size_t(std::min(y, h - 1)) * w + std::min(x, w - 1)) * 4 + k]); };
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++)
            for (int k = 0; k < 4; k++)
                out[(size_t(y) * ow + x) * 4 + k] = uint8_t(std::lround(
                    (at(2 * x, 2 * y, k) + at(2 * x + 1, 2 * y, k) + at(2 * x, 2 * y + 1, k) + at(2 * x + 1, 2 * y + 1, k)) / 4));
    w = ow; h = oh;
    return out;
}

static int maxDiff(const Bytes& a, const Bytes& b) {
    if (a.size() != b.size()) return 256;
    int m = 0;
    for (size_t i = 0; i < a.size(); i++) m = std::max(m, std::abs(int(a[i]) - int(b[i])));
    return m;
}

static double threadMs() {
    timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

struct Handoff { IOSurfaceRef io = nullptr; int generation = 0, w = 0, h = 0; uint64_t n = 0; };

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: present_test RAW\n"); return 2; }
    // The engine finds its shaders relative to the CMake build directory, as Program.cs does.
    char exe[4096]; uint32_t size = sizeof exe;
    _NSGetExecutablePath(exe, &size);
    for (auto d = std::filesystem::weakly_canonical(exe).parent_path(); d != d.parent_path(); d = d.parent_path())
        if (std::filesystem::exists(d / "build" / "CMakeCache.txt")) { std::filesystem::current_path(d / "build"); break; }

    @autoreleasepool {
        Shim* s = shim_open(argv[1]);
        Present* p = present_create();
        if (!s || !p) { std::printf("FAIL  shim_open or present_create\n"); return 1; }
        auto producer = (__bridge id<MTLSharedEvent>)present_producer(p);
        auto consumer = (__bridge id<MTLSharedEvent>)present_consumer(p);
        auto present = [&](int which, int level) {
            Handoff o;
            void* io = nullptr;
            if (present_frame(p, s, which, level, &io, &o.generation, &o.w, &o.h, &o.n) == 1) o.io = (IOSurfaceRef)io;
            return o;
        };
        auto landed = [&](const Handoff& o) { return [producer waitUntilSignaledValue:o.n timeoutMS:2000]; };
        auto release = [&](const Handoff& o) { consumer.signaledValue = o.n; };   // what Avalonia does after its snapshot
        char line[200];

        // 1 and 2. Both graphs at level 0; the full one also reduced.
        const int preview = shim_render(s, 1, nullptr), full = shim_render(s, 0, nullptr);
        check(preview == 1 && full == 2, "the engine rendered its preview and full graphs");
        for (int which : {preview, full}) {
            const Bytes cpu = render(s, which);
            int w = 0, h = 0;
            shim_read(s, which, nullptr, 0, &w, &h);
            for (int level = 0; level <= (which == full ? 2 : 0); level++) {
                const Handoff o = present(which, level);
                const bool ok = o.io && landed(o);
                Bytes want = cpu;
                int ww = w, wh = h;
                for (int k = 0; k < level; k++) want = halve(want, ww, wh);
                const int diff = ok ? maxDiff(surfaceBytes(o.io), want) : 256;
                std::snprintf(line, sizeof line, "%s graph, level %d: surface %dx%d is the CPU readback %s (max channel difference %d)",
                              which == 1 ? "preview" : "full", level, o.w, o.h, level ? "halved" : "byte for byte", diff);
                check(ok && o.w == ww && o.h == wh && diff <= level, line);
                if (ok) release(o);
            }
        }

        // 3 and 4. The hand-off, on the preview's surface.
        shim_set(s, "exposure_ev", 0, 0.0f);
        shim_render(s, 1, nullptr);
        const Bytes r1 = render(s, 1);
        const Handoff f1 = present(1, 0);
        check(f1.io && landed(f1) && surfaceBytes(f1.io) == r1, "frame 1 lands; the consumer has not released it");
        shim_set(s, "exposure_ev", 0, 1.0f);
        shim_render(s, 1, nullptr);
        const Bytes r2 = render(s, 1);
        const Handoff f2 = present(1, 0);
        check(f2.io == f1.io && r2 != r1, "frame 2 presented into the same surface, and it differs");
        check(![producer waitUntilSignaledValue:f2.n timeoutMS:300], "frame 2 held on the GPU until the consumer signals");
        check(surfaceBytes(f1.io) == r1, "the surface still holds frame 1 while frame 2 waits");
        shim_set(s, "exposure_ev", 0, 2.0f);
        auto next = std::async(std::launch::async, [&] { return shim_render(s, 1, nullptr); });
        check(next.wait_for(std::chrono::milliseconds(300)) == std::future_status::timeout,
              "the engine's next render waits while frame 2's copy is pending");
        release(f1);
        const bool ran = next.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        check(ran && landed(f2), "both go once the consumer signals frame 1");
        if (!ran) { std::printf("selftest FAILED (hung)\n"); std::fflush(stdout); _exit(1); }
        check(surfaceBytes(f1.io) == r2 && render(s, 1) != r2, "the surface holds frame 2, not the render made after it");
        release(f2);

        // 5. CPU per frame, this thread: hand-off against readback.
        std::printf("      CPU per frame on the calling thread, mean of 30 (wall in brackets):\n");
        for (auto [which, level, name] : {std::tuple{1, 0, "preview"}, {2, 0, "full"}, {2, 2, "full at fit (level 2)"}}) {
            double pc = 0, pw = 0, rc = 0, rw = 0;
            Bytes buf = render(s, which);
            int w, h;
            for (int i = 0; i < 30; i++) {
                double c0 = threadMs(); auto t0 = std::chrono::steady_clock::now();
                const Handoff o = present(which, level);
                pc += threadMs() - c0; pw += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                landed(o); release(o);
                c0 = threadMs(); t0 = std::chrono::steady_clock::now();
                shim_read(s, which, buf.data(), int(buf.size()), &w, &h);
                rc += threadMs() - c0; rw += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            }
            std::printf("      %-22s present_frame %.3f ms (%.3f)   shim_read %.3f ms (%.3f)\n", name, pc / 30, pw / 30, rc / 30, rw / 30);
        }
        present_destroy(p);
        shim_close(s);
    }
    std::printf("%s\n", failures ? "selftest FAILED" : "selftest passed");
    return failures ? 1 : 0;
}
