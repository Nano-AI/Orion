// Reading files back: lens database, black levels, export formats and metadata.
//
// Split out of main.cpp 2026-07-31; see harness.h.

#include "harness.h"

bool readBack(const std::string& path, int& bitsPerComponent, double rgb[3]) {
    CFStringRef p = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    if (p == nullptr) return false;
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, p, kCFURLPOSIXPathStyle, false);
    CFRelease(p);
    if (url == nullptr) return false;

    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (src == nullptr) return false;

    CGImageRef image = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (image == nullptr) return false;

    bitsPerComponent = static_cast<int>(CGImageGetBitsPerComponent(image));

    CGColorSpaceRef space = CGImageGetColorSpace(image);
    if (space == nullptr) { CGImageRelease(image); return false; }

    std::uint16_t pixel[4] = {0, 0, 0, 0};
    CGContextRef ctx = CGBitmapContextCreate(
        pixel, 1, 1, 16, sizeof pixel, space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast) | kCGBitmapByteOrder16Little);
    if (ctx == nullptr) { CGImageRelease(image); return false; }

    // Draw the whole image scaled down to the single pixel we sample. The test
    // image is a horizontal ramp with a constant red channel, so the average is
    // exactly as diagnostic as any one pixel and needs no coordinate care.
    CGContextDrawImage(ctx, CGRectMake(0, 0, 1, 1), image);
    CGContextRelease(ctx);
    CGImageRelease(image);

    for (int i = 0; i < 3; ++i) rgb[i] = double(pixel[i]) / 65535.0;
    return true;
}

/// Which metadata a written file carries: a GPS block, and the one EXIF field
/// the stand-in source below puts in.
///
/// The *field* rather than the EXIF dictionary, because ImageIO writes a small
/// EXIF block of its own — color space, pixel dimensions — into every JPEG
/// whatever it is handed. What matters is whether the camera's data came
/// across, not whether the container has an EXIF section at all.
bool metadataBlocks(const std::string& path, bool& gps, bool& exif) {
    gps = exif = false;
    CFStringRef p = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    if (p == nullptr) return false;
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, p, kCFURLPOSIXPathStyle, false);
    CFRelease(p);
    if (url == nullptr) return false;

    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (src == nullptr) return false;

    CFDictionaryRef all = CGImageSourceCopyPropertiesAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (all == nullptr) return false;

    gps = CFDictionaryContainsKey(all, kCGImagePropertyGPSDictionary);
    if (auto block = static_cast<CFDictionaryRef>(
            CFDictionaryGetValue(all, kCGImagePropertyExifDictionary))) {
        exif = CFDictionaryContainsKey(block, kCGImagePropertyExifFNumber);
    }
    CFRelease(all);
    return true;
}

/// A JPEG carrying a GPS block and one EXIF field, to stand in for a camera
/// file. Written here rather than taken from `samples/` because that folder is
/// local-only, and because the bodies in it have no GPS receiver.
bool writeJpegWithGps(const std::string& path, const std::uint16_t* rgba,
                      std::uint32_t width, std::uint32_t height, std::size_t bytesPerRow) {
    CFStringRef p = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    if (p == nullptr) return false;
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, p, kCFURLPOSIXPathStyle, false);
    CFRelease(p);
    if (url == nullptr) return false;

    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(
        const_cast<std::uint16_t*>(rgba), width, height, 16, bytesPerRow, space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast) | kCGBitmapByteOrder16Little);
    CGColorSpaceRelease(space);
    if (ctx == nullptr) { CFRelease(url); return false; }

    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    if (image == nullptr) { CFRelease(url); return false; }

    CGImageDestinationRef dst =
        CGImageDestinationCreateWithURL(url, CFSTR("public.jpeg"), 1, nullptr);
    CFRelease(url);
    if (dst == nullptr) { CGImageRelease(image); return false; }

    const double lat = 47.6205, lon = 122.3493, fnumber = 2.8;
    CFNumberRef latRef = CFNumberCreate(nullptr, kCFNumberDoubleType, &lat);
    CFNumberRef lonRef = CFNumberCreate(nullptr, kCFNumberDoubleType, &lon);
    CFNumberRef fRef   = CFNumberCreate(nullptr, kCFNumberDoubleType, &fnumber);

    const void* gpsKeys[] = {kCGImagePropertyGPSLatitude, kCGImagePropertyGPSLatitudeRef,
                             kCGImagePropertyGPSLongitude, kCGImagePropertyGPSLongitudeRef};
    const void* gpsVals[] = {latRef, CFSTR("N"), lonRef, CFSTR("W")};
    CFDictionaryRef gps = CFDictionaryCreate(nullptr, gpsKeys, gpsVals, 4,
                                             &kCFTypeDictionaryKeyCallBacks,
                                             &kCFTypeDictionaryValueCallBacks);

    const void* exifKeys[] = {kCGImagePropertyExifFNumber};
    const void* exifVals[] = {fRef};
    CFDictionaryRef exif = CFDictionaryCreate(nullptr, exifKeys, exifVals, 1,
                                              &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);

    // ⚠ A place name as well as the coordinates. GPS is not the only way a
    // file says where you were: anything catalogued carries the city and the
    // sub-location in words, and a "strip location" that removed the numbers
    // and left "Sub-location: <the street>" is a control that reads as honest
    // and is not.
    const void* iptcKeys[] = {kCGImagePropertyIPTCCity,
                              kCGImagePropertyIPTCSubLocation,
                              kCGImagePropertyIPTCKeywords};
    const void* iptcVals[] = {CFSTR("Seattle"), CFSTR("Pike Place"), CFSTR("test")};
    CFDictionaryRef iptc = CFDictionaryCreate(nullptr, iptcKeys, iptcVals, 3,
                                              &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);

    const void* keys[] = {kCGImagePropertyGPSDictionary, kCGImagePropertyExifDictionary,
                          kCGImagePropertyIPTCDictionary};
    const void* vals[] = {gps, exif, iptc};
    CFDictionaryRef props = CFDictionaryCreate(nullptr, keys, vals, 3,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);

    CGImageDestinationAddImage(dst, image, props);
    const bool ok = CGImageDestinationFinalize(dst);

    CFRelease(props); CFRelease(iptc); CFRelease(exif); CFRelease(gps);
    CFRelease(fRef); CFRelease(lonRef); CFRelease(latRef);
    CFRelease(dst);
    CGImageRelease(image);
    return ok;
}

/// Whether IPTC still names a place — the words, not the coordinates.
bool iptcNamesAPlace(const std::string& path) {
    CFStringRef p = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    if (p == nullptr) return false;
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, p, kCFURLPOSIXPathStyle, false);
    CFRelease(p);
    if (url == nullptr) return false;

    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (src == nullptr) return false;

    CFDictionaryRef all = CGImageSourceCopyPropertiesAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (all == nullptr) return false;

    bool found = false;
    if (auto block = static_cast<CFDictionaryRef>(
            CFDictionaryGetValue(all, kCGImagePropertyIPTCDictionary))) {
        found = CFDictionaryContainsKey(block, kCGImagePropertyIPTCCity)
             || CFDictionaryContainsKey(block, kCGImagePropertyIPTCSubLocation);
    }
    CFRelease(all);
    return found;
}

/// The whole written picture, as 16-bit RGBA, whatever depth the file holds.
///
/// `readBack` averages the image down to one pixel, which cannot see an edge —
/// and an edge is the only place output sharpening does anything.
bool decode16(const std::string& path, std::vector<std::uint16_t>& out,
              std::size_t& width, std::size_t& height) {
    CFStringRef p = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    if (p == nullptr) return false;
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, p, kCFURLPOSIXPathStyle, false);
    CFRelease(p);
    if (url == nullptr) return false;

    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (src == nullptr) return false;

    CGImageRef image = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (image == nullptr) return false;

    width = CGImageGetWidth(image);
    height = CGImageGetHeight(image);
    out.assign(width * height * 4, 0);

    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(
        out.data(), width, height, 16, width * 4 * sizeof(std::uint16_t), space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast) | kCGBitmapByteOrder16Little);
    CGColorSpaceRelease(space);
    if (ctx == nullptr) { CGImageRelease(image); return false; }

    CGContextDrawImage(ctx, CGRectMake(0, 0, static_cast<CGFloat>(width),
                                       static_cast<CGFloat>(height)), image);
    CGContextRelease(ctx);
    CGImageRelease(image);
    return true;
}

/// Black levels from LibRaw's three sources: a global offset, a per-channel
/// trim, and a 2D pattern.
///
/// The 2x2 pattern used to be averaged into one number and added to all four
/// channels. That is wrong whenever the pattern has real spread — it leaves the
/// spread in the shadows as a color cast, on every frame, with no control that
/// can remove it. The pattern lines up with the CFA cell for cell, so each
/// channel can have its own.
/// The vendored lensfun database, and the name matching that has to survive
/// EXIF spelling.
///
/// The matcher is the risky part, not the parser: a *confident wrong* profile
/// distorts the frame and labels the result "measured". So the assertions are
/// as much about what must not match as about what must.
void testLensDatabase() {
    section("Lens database");

    orion::pipe::LensDatabase db(std::string(ORION_DATA_DIR) + "/lensfun");
    report(db.loaded(), "the vendored database parses",
           std::to_string(db.lensCount()) + " lenses");
    if (!db.loaded()) return;

    report(db.lensCount() > 1000, "and it is the whole database, not one file",
           std::to_string(db.lensCount()) + " lenses");

    // A lens that is in it, spelled the way a Sony body writes it: no maker,
    // capital F, no slash.
    {
        const auto p = db.lookup("FE 24-70mm F2.8 GM", 35.0f, 4.0f);
        report(p.found, "a Sony zoom is found from the EXIF spelling", p.lens);
        if (p.found) {
            report(p.lens.find("24-70") != std::string::npos,
                   "and it is the 24-70, not another lens", p.lens);
            const bool moves = p.poly[0] != 0.0f || p.poly[1] != 0.0f || p.poly[2] != 0.0f;
            report(moves, "with distortion coefficients that are not all zero");
        }
    }

    // Vignetting interpolates across aperture rather than snapping to the
    // nearest calibrated stop, and does it in the reciprocal — which is what
    // lensfun does, because the f-number is a ratio and vignetting tracks the
    // entrance pupil.
    //
    // Nearest-stop is invisible by inspection and obvious here: a lens
    // calibrated at two stops renders every aperture between them identically,
    // then jumps. This asks whether an intermediate aperture lands strictly
    // between its neighbors.
    {
        // A prime with vignetting calibrated at many stops. The Sony zoom
        // above has distortion data but no vignetting, which is common — the
        // database is not uniformly populated, and a test that assumed it was
        // measured nothing.
        const char* lens = "smc Pentax-FA 50mm f/1.4";
        const float focal = 50.0f;
        const auto wide  = db.lookup(lens, focal, 2.0f);
        const auto mid   = db.lookup(lens, focal, 2.4f);   // between 2 and 2.8
        const auto tight = db.lookup(lens, focal, 2.8f);

        if (wide.found && mid.found && tight.found) {
            const float a = wide.vignette[0], b = mid.vignette[0], c = tight.vignette[0];
            std::printf("  vignette p_a: f/2 %.4f, f/2.4 %.4f (uncalibrated), f/2.8 %.4f\n", a, b, c);

            // Vignetting weakens as the lens stops down, so the coefficients
            // should be ordered and the middle one should be neither endpoint.
            const bool between = (b != a) && (b != c);
            report(between, "an uncalibrated aperture is interpolated, not snapped",
                   std::to_string(a) + " / " + std::to_string(b) + " / " + std::to_string(c));

            const bool ordered = (a < b && b < c) || (a > b && b > c);
            report(ordered, "and lands between its neighbors rather than outside them",
                   std::to_string(a) + " / " + std::to_string(b) + " / " + std::to_string(c));
        } else {
            report(false, "the calibrated prime is found at three apertures",
                   "lookup failed");
        }
    }

    // Interpolation across the zoom range: a 24-70 does not distort the same
    // way at both ends, so two focal lengths must not return one answer.
    {
        const auto wide = db.lookup("FE 24-70mm F2.8 GM", 24.0f, 4.0f);
        const auto tele = db.lookup("FE 24-70mm F2.8 GM", 70.0f, 4.0f);
        if (wide.found && tele.found) {
            const bool differ = wide.poly[0] != tele.poly[0] ||
                                wide.poly[1] != tele.poly[1] ||
                                wide.poly[2] != tele.poly[2];
            report(differ, "the correction changes with focal length");
        } else {
            report(false, "both ends of the zoom are found");
        }
    }

    // What must NOT match.
    report(!db.lookup("", 24.0f, 4.0f).found, "an empty lens name finds nothing");
    report(!db.lookup("50mm", 50.0f, 2.0f).found,
           "a name too short to identify a lens finds nothing");
    report(!db.lookup("Wobbleflex Hyperprime 404mm", 404.0f, 4.0f).found,
           "a lens that does not exist finds nothing");

    // The developer's own lens is a mirrorless Sigma that is not in the
    // database. The DSLR "24mm f/1.4 DG HSM" is a different optical design, and
    // matching it would apply one lens's distortion to another's picture.
    {
        const auto p = db.lookup("24mm F1.4 DG DN | Art 022", 24.0f, 4.0f);
        if (p.found) {
            report(p.lens.find("DG DN") != std::string::npos,
                   "a DG DN lens never matches a DG HSM entry", p.lens);
        } else {
            report(true, "an uncalibrated lens reports no profile rather than a guess");
        }
    }
}

void testBlackLevels() {
    section("Black levels");

    using orion::raw::BayerImage;

    // RGGB: red at (0,0), green at (1,0) and (0,1), blue at (1,1).
    constexpr std::uint32_t kRggb = 0x94949494;

    // No pattern at all: global plus per-channel trim, nothing else.
    {
        unsigned cblack[10] = {1, 2, 3, 4, 0, 0, 0, 0, 0, 0};
        const auto b = BayerImage::blackLevels(100, cblack, kRggb);
        report(b[0] == 101 && b[1] == 102 && b[2] == 103 && b[3] == 104,
               "with no pattern each channel is the global black plus its own trim");
    }

    // A 2x2 pattern, row-major over the CFA cell.
    {
        unsigned cblack[10] = {0, 0, 0, 0, 2, 2, 512, 520, 530, 540};
        const auto b = BayerImage::blackLevels(0, cblack, kRggb);
        report(b[0] == 512, "the red cell takes the red pattern value",
               "got " + std::to_string(b[0]));
        report(b[2] == 540, "the blue cell takes the blue pattern value",
               "got " + std::to_string(b[2]));
        report(b[1] == 520, "green takes a green pattern value",
               "got " + std::to_string(b[1]));

        // The bug this replaces: every channel got (512+520+530+540)/4 = 525,
        // so red sat 13 counts high and blue 15 counts low.
        report(b[0] != b[2], "red and blue no longer collapse to the same number");
    }

    // A pattern larger than the CFA cell has no cell-by-cell answer, so it is
    // averaged — stated rather than silent.
    {
        unsigned cblack[42] = {0, 0, 0, 0, 6, 6};
        for (int i = 0; i < 36; ++i) cblack[6 + i] = 100u + unsigned(i);
        const auto b = BayerImage::blackLevels(0, cblack, kRggb);
        report(b[0] == 117 && b[1] == 117 && b[2] == 117,
               "a pattern bigger than the mosaic cell falls back to its mean",
               "got " + std::to_string(b[0]));
    }

    // BGGR, to prove the mapping follows the mosaic rather than the order the
    // values happen to be stored in.
    {
        constexpr std::uint32_t kBggr = 0x16161616;
        unsigned cblack[10] = {0, 0, 0, 0, 2, 2, 512, 520, 530, 540};
        const auto b = BayerImage::blackLevels(0, cblack, kBggr);
        report(b[2] == 512, "on a BGGR sensor the blue cell takes the first value",
               "got " + std::to_string(b[2]));
        report(b[0] == 540, "and red takes the last",
               "got " + std::to_string(b[0]));
    }
}

void testExportFormats() {
    section("Export");

    using orion::util::ImageFormat;
    report(orion::util::formatForPath("a.png")  == ImageFormat::Png,  "png by extension");
    report(orion::util::formatForPath("a.PNG")  == ImageFormat::Png,  "extension is case-insensitive");
    report(orion::util::formatForPath("a.tif")  == ImageFormat::Tiff, "tif by extension");
    report(orion::util::formatForPath("a.tiff") == ImageFormat::Tiff, "tiff by extension");
    report(orion::util::formatForPath("a.jpg")  == ImageFormat::Jpeg, "jpg by extension");
    report(orion::util::formatForPath("noext")  == ImageFormat::Jpeg, "no extension defaults to jpeg");
    report(orion::util::formatForPath("a.b.png") == ImageFormat::Png, "uses the last extension");

    // ── Color space and depth on the way out ───────────────────────────────
    //
    // Both of these fail silently. A file tagged with the wrong profile opens
    // shifted in one application and correct in another, and a resize that
    // quietly drops to eight bits undoes the 16-bit path for exactly the
    // exports — the smaller ones — where nobody thinks to check.
    using orion::util::ColorSpace;

    constexpr std::uint32_t kW = 64, kH = 48;
    std::vector<std::uint16_t> pixels(std::size_t(kW) * kH * 4);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = (std::size_t(y) * kW + x) * 4;
            // A saturated red, which is where two profiles disagree most.
            pixels[i + 0] = 65535;
            pixels[i + 1] = static_cast<std::uint16_t>(x * 400);
            pixels[i + 2] = 0;
            pixels[i + 3] = 65535;
        }
    }
    const std::size_t stride = std::size_t(kW) * 4 * sizeof(std::uint16_t);

    const std::string dir = "/tmp/";
    struct Case { ColorSpace space; const char* name; };
    const Case cases[] = {
        {ColorSpace::Srgb,      "sRGB"},
        {ColorSpace::DisplayP3, "Display P3"},
        {ColorSpace::AdobeRgb,  "Adobe RGB"},
    };

    // Read the written file back rather than inferring from its size. A PNG of
    // a smooth ramp compresses to almost nothing at either depth, so byte
    // counts cannot tell eight bits from sixteen — which is exactly how the
    // resize path lost its depth without any test noticing.
    double red[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        orion::util::ExportOptions o{};
        o.format = ImageFormat::Png;
        o.space = cases[i].space;

        const std::string path = dir + "orion-space-" + std::to_string(i) + ".png";
        try {
            orion::util::writeImage(path, pixels.data(), kW, kH, stride, o);
        } catch (const std::exception& e) {
            report(false, std::string("writes ") + cases[i].name, e.what());
            continue;
        }

        int bits = 0;
        double rgb[3] = {0, 0, 0};
        if (!readBack(path, bits, rgb)) {
            report(false, std::string("reads back ") + cases[i].name);
            continue;
        }

        report(bits == 16, std::string("a ") + cases[i].name + " export is 16-bit",
               "got " + std::to_string(bits));
        report(orion::util::encodedSize(pixels.data(), kW, kH, stride, o) > 0,
               std::string("encodes to ") + cases[i].name);

        red[i] = rgb[0];
    }

    // sRGB's full red is inside the gamut of both wider spaces, so expressing
    // it there needs *less* than full red — around 0.92 in P3. A number that
    // stayed at 1.0 would mean the file had been relabeled rather than
    // converted, which is the failure worth catching: it opens oversaturated
    // in every application that honours the profile.
    report(red[0] > 0.99, "sRGB keeps a saturated red at full",
           "got " + std::to_string(red[0]));
    report(red[1] < 0.97 && red[1] > 0.80,
           "the same red is inside Display P3, so it sits below full",
           "got " + std::to_string(red[1]));
    report(red[2] < 0.97 && red[2] > 0.70,
           "the same red is inside Adobe RGB, so it sits below full",
           "got " + std::to_string(red[2]));

    // ── Metadata policy ────────────────────────────────────────────────────
    //
    // Location is the one metadata field with consequences: a photo taken at
    // home carries the home coordinates, and no viewer says so. The sample
    // frames have no GPS — the body has no receiver — so the source is written
    // here with a GPS block, which also makes the test run anywhere.
    {
        using orion::util::Metadata;

        const std::string source = dir + "orion-meta-source.jpg";
        report(writeJpegWithGps(source, pixels.data(), kW, kH, stride),
               "a stand-in camera file with GPS is written");

        bool gps = false, exif = false;
        report(metadataBlocks(source, gps, exif) && gps,
               "the stand-in source carries GPS");

        struct MetaCase { Metadata policy; bool wantGps; bool wantExif; const char* name; };
        const MetaCase metaCases[] = {
            {Metadata::All,        true,  true,  "keep all"},
            {Metadata::NoLocation, false, true,  "strip location"},
            {Metadata::None,       false, false, "strip everything"},
        };
        for (const auto& m : metaCases) {
            orion::util::ExportOptions o{};
            o.format = ImageFormat::Jpeg;
            o.metadataFrom = source;
            o.metadata = m.policy;
            const std::string path = dir + "orion-meta-" + std::to_string(int(m.policy)) + ".jpg";
            try {
                orion::util::writeImage(path, pixels.data(), kW, kH, stride, o);
            } catch (const std::exception& e) {
                report(false, std::string("writes with ") + m.name, e.what());
                continue;
            }
            if (!metadataBlocks(path, gps, exif)) {
                report(false, std::string("reads back ") + m.name);
                continue;
            }
            report(gps == m.wantGps,
                   std::string(m.name) + (m.wantGps ? " keeps GPS" : " removes GPS"));
            report(exif == m.wantExif,
                   std::string(m.name) +
                       (m.wantExif ? " keeps the camera EXIF" : " removes the camera EXIF"));
            // ⚠ The words, not just the coordinates. This was the hole: the
            // GPS block was dropped and IPTC's city and sub-location went
            // straight through, so "Strip location" published the place name of
            // every catalogued photo while reporting itself as working.
            report(iptcNamesAPlace(path) == m.wantGps,
                   std::string(m.name) +
                       (m.wantGps ? " keeps the IPTC place name"
                                  : " removes the IPTC place name"));
        }

        // The default is the safe one. This is the assertion that matters: the
        // failure mode is silent, and it publishes someone's address.
        orion::util::ExportOptions fresh{};
        report(fresh.metadata == Metadata::NoLocation,
               "an export that says nothing about metadata strips location");
    }

    // A resize must not cost the depth. It used to: the resize context was
    // eight bits, so every export with a size limit quietly halved its
    // precision — and the smaller exports are the ones nobody re-checks.
    orion::util::ExportOptions resized{};
    resized.format = ImageFormat::Png;
    resized.maxDimension = 32;
    const std::string smallPath = dir + "orion-space-small.png";
    try {
        orion::util::writeImage(smallPath, pixels.data(), kW, kH, stride, resized);
        int bits = 0;
        double rgb[3] = {0, 0, 0};
        if (readBack(smallPath, bits, rgb)) {
            report(bits == 16, "a resized export keeps sixteen bits",
                   "got " + std::to_string(bits));
        } else {
            report(false, "a resized export reads back");
        }
    } catch (const std::exception& e) {
        report(false, "a resized export writes", e.what());
    }

    // ── Bit depth ──────────────────────────────────────────────────────────
    //
    // ⚠ Both directions, and the second one is the point. Until this control
    // existed the writer could only produce sixteen bits, so "16 means 16"
    // was true of code that had no choice — and every PNG Orion wrote was
    // twice the size it needed to be with nothing in the interface saying so.
    using orion::util::BitDepth;
    {
        report(orion::util::ExportOptions{}.depth == BitDepth::Sixteen,
               "an export that says nothing about depth keeps sixteen bits");

        struct DepthCase { BitDepth depth; std::uint32_t maxDim; int want; const char* name; };
        const DepthCase depthCases[] = {
            {BitDepth::Eight,   0,  8,  "eight bits"},
            {BitDepth::Sixteen, 0,  16, "sixteen bits"},
            // Through the resize path too, which is where the depth was lost
            // once before and is the more complicated of the two routes.
            {BitDepth::Eight,   32, 8,  "eight bits, resized"},
            {BitDepth::Sixteen, 32, 16, "sixteen bits, resized"},
        };
        for (const auto& d : depthCases) {
            orion::util::ExportOptions o{};
            o.format = ImageFormat::Png;
            o.depth = d.depth;
            o.maxDimension = d.maxDim;
            const std::string path = dir + "orion-depth-" + std::to_string(d.want)
                                   + "-" + std::to_string(d.maxDim) + ".png";
            try {
                orion::util::writeImage(path, pixels.data(), kW, kH, stride, o);
            } catch (const std::exception& e) {
                report(false, std::string("writes ") + d.name, e.what());
                continue;
            }
            int bits = 0;
            double rgb[3] = {0, 0, 0};
            if (!readBack(path, bits, rgb)) {
                report(false, std::string("reads back ") + d.name);
                continue;
            }
            report(bits == d.want, std::string("a PNG asked for ") + d.name + " is that",
                   "got " + std::to_string(bits));
        }
    }

    // ── Output sharpening ──────────────────────────────────────────────────
    //
    // On a step edge, which is the only place an unsharp mask does anything. A
    // ramp — what the rest of this section uses — has no edge to overshoot, so
    // it would measure a working sharpener as a dead one.
    //
    // The overshoot either side of the edge is the unsharp mask's signature.
    // Asserting on it rather than on "the file changed" means a pass that
    // blurred, or that added a constant, cannot be mistaken for one that
    // sharpened.
    using orion::util::Sharpen;
    {
        // ⚠ The edge is in **green only**, and that is what makes the
        // luminance-only claim checkable. Red and blue are flat across the
        // frame, so a sharpener that worked per channel would leave them flat —
        // and one that works on luminance, as this one does, has to push them
        // up and down alongside green. A neutral edge cannot tell the two
        // apart: on grey they compute the same answer, so the assertion would
        // pass on either implementation and prove nothing.
        constexpr std::uint32_t kEw = 64, kEh = 32;
        constexpr std::uint16_t kFlat = 32768;
        std::vector<std::uint16_t> edge(std::size_t(kEw) * kEh * 4);
        for (std::uint32_t y = 0; y < kEh; ++y) {
            for (std::uint32_t x = 0; x < kEw; ++x) {
                const std::size_t i = (std::size_t(y) * kEw + x) * 4;
                // Well inside the range, so an overshoot has room to exist
                // rather than being clipped at either end.
                edge[i + 0] = kFlat;
                edge[i + 1] = x < kEw / 2 ? 16384 : 49151;
                edge[i + 2] = kFlat;
                edge[i + 3] = 65535;
            }
        }
        const std::size_t edgeStride = std::size_t(kEw) * 4 * sizeof(std::uint16_t);

        struct SharpCase { Sharpen mode; const char* name; };
        const SharpCase sharpCases[] = {
            {Sharpen::None,   "none"},
            {Sharpen::Screen, "screen"},
            {Sharpen::Print,  "print"},
        };

        double overshoot[3] = {0, 0, 0};
        double meanLuma[3] = {0, 0, 0};
        double flatChannelSwing[3] = {0, 0, 0};

        for (int i = 0; i < 3; ++i) {
            orion::util::ExportOptions o{};
            o.format = ImageFormat::Png;
            o.sharpen = sharpCases[i].mode;
            const std::string path = dir + "orion-sharpen-" + sharpCases[i].name + ".png";
            try {
                orion::util::writeImage(path, edge.data(), kEw, kEh, edgeStride, o);
            } catch (const std::exception& e) {
                report(false, std::string("writes sharpening ") + sharpCases[i].name, e.what());
                continue;
            }

            std::vector<std::uint16_t> got;
            std::size_t gw = 0, gh = 0;
            if (!decode16(path, got, gw, gh) || gw != kEw || gh != kEh) {
                report(false, std::string("reads back sharpening ") + sharpCases[i].name);
                continue;
            }

            // The brightest green on the light side of the edge, against the
            // flat value it started at — and how far the two channels that
            // never had an edge were moved.
            double peak = 0.0, total = 0.0, swing = 0.0;
            for (std::size_t y = 0; y < gh; ++y) {
                for (std::size_t x = 0; x < gw; ++x) {
                    const std::size_t p = (y * gw + x) * 4;
                    const double r = got[p], g = got[p + 1], b = got[p + 2];
                    total += (0.2126 * r + 0.7152 * g + 0.0722 * b) / 65535.0;
                    swing = std::max(swing, std::abs(r - double(kFlat)));
                    swing = std::max(swing, std::abs(b - double(kFlat)));
                    if (x >= gw / 2) peak = std::max(peak, g);
                }
            }
            overshoot[i] = (peak - 49151.0) / 65535.0;
            meanLuma[i] = total / double(gw * gh);
            flatChannelSwing[i] = swing / 65535.0;
        }

        report(overshoot[0] < 1e-4, "None leaves the edge exactly as it was",
               "overshoot " + std::to_string(overshoot[0]));
        report(overshoot[1] > overshoot[0], "Screen overshoots the edge",
               "got " + std::to_string(overshoot[1]));
        report(overshoot[2] > overshoot[1], "Print overshoots more than Screen",
               "got " + std::to_string(overshoot[2]) + " vs "
                      + std::to_string(overshoot[1]));

        // ⚠ An unsharp mask is a high-pass added back: it moves edges and
        // leaves the overall brightness where it was. A pass that lifted the
        // whole frame would satisfy every check above — and would be an
        // exposure change wearing a sharpening control's label.
        for (int i = 1; i < 3; ++i) {
            report(std::abs(meanLuma[i] - meanLuma[0]) < 0.01,
                   std::string("sharpening ") + sharpCases[i].name
                       + " does not move the overall brightness",
                   "got " + std::to_string(meanLuma[i]) + " vs "
                          + std::to_string(meanLuma[0]));
            // ⚠ Luminance only, which means red and blue move with green even
            // though neither has an edge of its own. Sharpening the three
            // channels independently would leave them exactly flat — and would
            // put colored fringes on exactly the high-contrast edges this
            // control is aimed at.
            report(flatChannelSwing[i] > 0.005,
                   std::string("sharpening ") + sharpCases[i].name
                       + " moves all three channels together, not each alone",
                   "flat-channel swing " + std::to_string(flatChannelSwing[i]));
        }

        // And None touches nothing at all: the channels that had no edge are
        // exactly where they started.
        report(flatChannelSwing[0] < 1e-4,
               "None leaves the flat channels exactly flat",
               "swing " + std::to_string(flatChannelSwing[0]));

        orion::util::ExportOptions plain{};
        plain.format = ImageFormat::Png;
        report(orion::util::encodedSize(edge.data(), kEw, kEh, edgeStride, plain) > 0,
               "the unsharpened edge encodes");
    }
}

// ── Orientation on the GPU ─────────────────────────────────────────────────

/// Runs the real orient kernel over a synthetic image whose pixels encode
/// their own coordinates, then checks that each output pixel came from where
/// the rotation says it should.
///
/// This is the test that would have caught the torn, sideways frame: the maths
/// tests above all pass on code that renders garbage, because they never touch
/// a texture.
