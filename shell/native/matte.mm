// Subject and person selection through Apple's Vision framework, for the
// shell: the inference half of app/SubjectMatte.swift. research/masking.md §5.
// Vision is macOS-only; the C# side says "not available on this system" elsewhere.
//
// No Objective-C exception or C++ exception leaves this file: a failure is a 0
// and a sentence in `why`.
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#include <algorithm>
#include <cstdio>

static int fail(char* why, int capacity, NSString* text) {
    if (why && capacity > 0) std::snprintf(why, size_t(capacity), "%s", text.UTF8String ?: "Vision failed.");
    return 0;
}

// A Vision mask buffer as coverage on the picture's own grid, nearest-neighbour:
// the person request answers at the model's resolution, not the input's, and
// the guided refinement downstream is what recovers the edge (SubjectMatte.swift `floats`).
static bool coverage(CVPixelBufferRef buffer, float* alpha, int width, int height) {
    CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    const size_t bw = CVPixelBufferGetWidth(buffer), bh = CVPixelBufferGetHeight(buffer);
    const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
    const auto* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(buffer));
    const bool isFloat = CVPixelBufferGetPixelFormatType(buffer) == kCVPixelFormatType_OneComponent32Float;
    if (base && bw > 0 && bh > 0)
        for (int y = 0; y < height; ++y) {
            const size_t sy = std::min(bh - 1, size_t(y) * bh / size_t(height));
            for (int x = 0; x < width; ++x) {
                const size_t sx = std::min(bw - 1, size_t(x) * bw / size_t(width));
                const float v = isFloat ? reinterpret_cast<const float*>(base + sy * stride)[sx]
                                        : float(base[sy * stride + sx]) / 255.0f;
                alpha[size_t(y) * size_t(width) + size_t(x)] = std::clamp(v, 0.0f, 1.0f);
            }
        }
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    return base && bw > 0 && bh > 0;
}

extern "C" {

// `rgba`: width * height opaque 8-bit pixels, top-left origin, display-referred
// (the developed render). `kind` 0 subject, 1 person. Fills `alpha`
// (width * height) and returns 1; else 0 with the reason in `why`. An empty
// subject answer is "nothing found", said, not an empty matte.
int shim_vision_matte(const uint8_t* rgba, int width, int height, int kind,
                      float* alpha, char* why, int why_capacity) {
    if (!rgba || !alpha || width <= 0 || height <= 0) return fail(why, why_capacity, @"Could not read the rendered picture.");
    @autoreleasepool {
        @try {
            CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(const_cast<uint8_t*>(rgba), size_t(width), size_t(height), 8,
                                                     size_t(width) * 4, space, kCGImageAlphaNoneSkipLast);
            CGImageRef image = ctx ? CGBitmapContextCreateImage(ctx) : nullptr;
            if (ctx) CGContextRelease(ctx);
            CGColorSpaceRelease(space);
            if (!image) return fail(why, why_capacity, @"Could not read the rendered picture.");

            VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCGImage:image options:@{}];
            CGImageRelease(image);
            NSError* error = nil;
            CVPixelBufferRef buffer = nullptr;
            if (kind == 0) {
                if (@available(macOS 14.0, *)) {
                    auto* request = [[VNGenerateForegroundInstanceMaskRequest alloc] init];
                    if (![handler performRequests:@[request] error:&error]) return fail(why, why_capacity, error.localizedDescription);
                    VNInstanceMaskObservation* observation = request.results.firstObject;
                    if (!observation || observation.allInstances.count == 0)
                        return fail(why, why_capacity, @"Nothing was found to select in this photograph.");
                    buffer = [observation generateScaledMaskForImageForInstances:observation.allInstances
                                                              fromRequestHandler:handler error:&error];
                    if (!buffer) return fail(why, why_capacity, error.localizedDescription);
                    const bool ok = coverage(buffer, alpha, width, height);
                    CVPixelBufferRelease(buffer);   // generateScaledMask… returns +1
                    return ok ? 1 : fail(why, why_capacity, @"Could not read the rendered picture.");
                }
                return fail(why, why_capacity, @"Subject detection needs macOS 14.");
            }
            auto* request = [[VNGeneratePersonSegmentationRequest alloc] init];
            request.qualityLevel = VNGeneratePersonSegmentationRequestQualityLevelAccurate;
            request.outputPixelFormat = kCVPixelFormatType_OneComponent8;
            if (![handler performRequests:@[request] error:&error]) return fail(why, why_capacity, error.localizedDescription);
            VNPixelBufferObservation* observation = request.results.firstObject;
            if (!observation) return fail(why, why_capacity, @"Nothing was found to select in this photograph.");
            return coverage(observation.pixelBuffer, alpha, width, height) ? 1
                 : fail(why, why_capacity, @"Could not read the rendered picture.");
        } @catch (NSException* e) {
            return fail(why, why_capacity, e.reason ?: @"Vision failed.");
        }
    }
}

// Vision's face rectangles on the same kind of picture, for `Orion --agent
// faces` (app/AgentFaces.swift's detector). `boxes` takes four floats a face,
// Vision's own normalised x, y, w, h with a bottom-left origin. Returns the
// number found (it may exceed `capacity`, which caps what is written), or -1
// with the reason in `why`.
int shim_vision_faces(const uint8_t* rgba, int width, int height, float* boxes, int capacity,
                      char* why, int why_capacity) {
    if (!rgba || width <= 0 || height <= 0) { fail(why, why_capacity, @"Could not read the rendered picture."); return -1; }
    @autoreleasepool {
        @try {
            CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(const_cast<uint8_t*>(rgba), size_t(width), size_t(height), 8,
                                                     size_t(width) * 4, space, kCGImageAlphaNoneSkipLast);
            CGImageRef image = ctx ? CGBitmapContextCreateImage(ctx) : nullptr;
            if (ctx) CGContextRelease(ctx);
            CGColorSpaceRelease(space);
            if (!image) { fail(why, why_capacity, @"Could not read the rendered picture."); return -1; }
            VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCGImage:image options:@{}];
            CGImageRelease(image);
            auto* request = [[VNDetectFaceRectanglesRequest alloc] init];
            NSError* error = nil;
            if (![handler performRequests:@[request] error:&error]) { fail(why, why_capacity, error.localizedDescription); return -1; }
            int n = 0;
            for (VNFaceObservation* face in request.results) {
                if (boxes && n < capacity) {
                    const CGRect b = face.boundingBox;
                    float* o = boxes + 4 * n;
                    o[0] = float(b.origin.x); o[1] = float(b.origin.y); o[2] = float(b.size.width); o[3] = float(b.size.height);
                }
                ++n;
            }
            return n;
        } @catch (NSException* e) {
            fail(why, why_capacity, e.reason ?: @"Vision failed.");
            return -1;
        }
    }
}

}  // extern "C"
