// Headless check of the Dawn side: no window, no Avalonia.
// Drives the same C facade the app uses, then locks the IOSurface and reads it
// on the CPU. That readback is evidence only and exists nowhere but here.
//   1. frame 1 lands in the IOSurface: corner marker, sweeping bar, colour field
//   2. frame 2 waits on the GPU for the consumer event before it writes
//   3. frame 2 then lands, and differs from frame 1
//   4. torn down and re-created at 6000x4000, a frame still lands
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurfaceObjC.h>
#import <Metal/Metal.h>
#include <cmath>
#include <cstdio>
#include <cstdint>

extern "C" {
int ds_create(int, int);
const char* ds_error();
void* ds_iosurface();
void* ds_consumer_event();
int ds_frame(float, uint64_t, void**, uint64_t*);
void ds_destroy();
}

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) failures++;
}

struct Px { int b, g, r; };
static Px at(IOSurfaceRef s, int x, int y) {
    auto row = (uint8_t*)IOSurfaceGetBaseAddress(s) + y * IOSurfaceGetBytesPerRow(s);
    return {row[x * 4], row[x * 4 + 1], row[x * 4 + 2]};
}
static bool near(Px p, int r, int g, int b, int tol = 3) {
    return std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol && std::abs(p.b - b) <= tol;
}
// The shader's colour field at pixel (x, y), for a value check.
static Px field(int x, int y, int w, int h, float t) {
    float u = (x + 0.5f) / w, v = (y + 0.5f) / h, phase[3] = {0, 0.33f, 0.67f};
    int c[3];
    for (int i = 0; i < 3; i++)
        c[i] = (int)lround(255 * (0.5f + 0.5f * cosf(6.2831f * (phase[i] + u + 0.5f * v + 0.25f * t))));
    return {c[2], c[1], c[0]};
}

int main() {
    @autoreleasepool {
        const int w = 1200, h = 800;
        if (ds_create(w, h) != 1) { printf("FAIL  ds_create: %s\n", ds_error()); return 1; }
        auto surface = (IOSurfaceRef)ds_iosurface();
        auto consumer = (__bridge id<MTLSharedEvent>)ds_consumer_event();

        void* ev; uint64_t value;
        check(ds_frame(1.0f, 0, &ev, &value) == 1, "frame 1 submitted");
        auto producer = (__bridge id<MTLSharedEvent>)ev;
        check([producer waitUntilSignaledValue:value timeoutMS:2000], "frame 1 producer event signalled");

        IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr);
        check(near(at(surface, 10, h - 10), 0, 0, 0), "frame 1: bottom-left corner is black (not flipped)");
        check(near(at(surface, w / 4, h / 2), 255, 255, 255), "frame 1: bar at x = w/4 (t = 1 s)");
        Px f = field(3 * w / 4, h / 2, w, h, 1.0f), p = at(surface, 3 * w / 4, h / 2);
        check(near(p, f.r, f.g, f.b), "frame 1: colour field matches the shader formula");
        printf("      pixel (%d,%d) rgb %d %d %d, formula %d %d %d\n", 3 * w / 4, h / 2, p.r, p.g, p.b, f.r, f.g, f.b);
        Px before = at(surface, w / 2, h / 2);
        IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);

        // Frame 2 must wait for the consumer (Avalonia, here us) to release frame 1.
        check(ds_frame(2.0f, 1, &ev, &value) == 1, "frame 2 submitted");
        producer = (__bridge id<MTLSharedEvent>)ev;
        check(![producer waitUntilSignaledValue:value timeoutMS:300], "frame 2 held on the GPU until the consumer signals");
        consumer.signaledValue = 1;
        check([producer waitUntilSignaledValue:value timeoutMS:2000], "frame 2 ran once the consumer signalled");

        IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr);
        check(near(at(surface, w / 2, h / 2), 255, 255, 255), "frame 2: bar moved to x = w/2 (t = 2 s)");
        check(!near(at(surface, w / 4, h / 2), 255, 255, 255), "frame 2: bar no longer at x = w/4");
        check(!near(at(surface, w / 2, h / 2), before.r, before.g, before.b), "frame 2 differs from frame 1");
        IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);

        // The windowed demo's second phase: tear down, start again at 24 MP.
        ds_destroy();
        const int W = 6000, H = 4000;
        check(ds_create(W, H) == 1, "re-created at 6000x4000");
        surface = (IOSurfaceRef)ds_iosurface();
        check(ds_frame(3.0f, 0, &ev, &value) == 1 &&
              [(__bridge id<MTLSharedEvent>)ev waitUntilSignaledValue:value timeoutMS:2000], "24 MP frame rendered");
        IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr);
        check(IOSurfaceGetWidth(surface) == W && near(at(surface, 10, H - 10), 0, 0, 0) &&
              near(at(surface, 3 * W / 4, H / 2), 255, 255, 255), "24 MP frame: corner black, bar at x = 3w/4 (t = 3 s)");
        IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);
    }
    printf("%s\n", failures ? "selftest FAILED" : "selftest passed");
    return failures ? 1 : 0;
}
