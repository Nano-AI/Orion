# LICENSE AUDIT — Orion RAW Editor

**Date:** 2026-09-13  
**Purpose:** Verify proprietary binary distribution compatibility  
**Transition:** From Apache-2.0 (commit abf3a51) to closed-source  

⚠ **Written against the SwiftUI app, retired by decision #380.** Its `app/`
paths are as of the audit. What the shipping bundle carries is now enumerated
by `tools/package-app.sh`, which fails on any file with no licence mapped, and
listed in `NOTICE`.

---

## What relicensing does NOT do

Orion was Apache-2.0 through commit abf3a51. That grant is irrevocable: anyone who downloaded a copy before 2026-09-13 may use, modify and redistribute it under Apache-2.0 forever. Going proprietary today does not revoke existing rights. The repository had 0 forks and 1 star at transition (unverified from CLI).

**This is not legal advice.** This memo documents *provenance* only — which dependencies permit closed-source distribution. Patents are mentioned once, where a decision already noticed them.

---

## Summary table

| Component | License | Link Mode | Proprietary OK | Condition |
|-----------|---------|-----------|----------------|-----------|
| **LibRaw** | CDDL-1.0 (chosen) | Dynamic | ✓ Yes | CDDL permits proprietary combination |
| **OpenCV** | Apache-2.0 | Dynamic | ✓ Yes | — |
| **libomp** | Apache-2.0 + LLVM-exception | Dynamic | ✓ Yes | — |
| **libjpeg-turbo** | JPEG/BSD-like | Dynamic | ✓ Yes | — |
| **liblcms2** | MIT | Dynamic | ✓ Yes | — |
| **libprotobuf** | BSD-3-Clause | Dynamic | ✓ Yes | — |
| **libpugixml** | MIT | Dynamic | ✓ Yes | — |
| **libtbb** | Apache-2.0 | Dynamic | ✓ Yes | — |
| **libopenblas** | BSD-3-Clause | Dynamic | ✓ Yes | — |
| **libabsl** (Abseil) | Apache-2.0 | Dynamic | ✓ Yes | — |
| **libopenvino** | Apache-2.0 | Dynamic | ✓ Yes | — |
| **libgfortran / libgcc_s / libquadmath** | GPL + runtime exception | Dynamic | ✓ Yes | GCC runtime exception permits proprietary linking |
| **SQLite3** | Public domain | Framework | ✓ Yes | — |
| **Slang** | Apache-2.0 + LLVM-exception | Build-time only | ✓ Yes | Not in binary, compiler only |
| **Lensfun data** | CC BY-SA 3.0 | Data file | ⚠ Conditional | Share-alike binds *data*, not app. Fixes must be contributed back. |
| **DNG spec usage** | Adobe DNG grant | Algorithm | ✓ Yes | NOTICE credit required; see below |

**Total components inventoried: 16** (one conditional, one with notice requirement)

---

## Component details

### Direct dependencies (CMakeLists.txt)

**LibRaw** (engine/CMakeLists.txt:60–72)  
- Upstream license: LGPL-2.1 OR CDDL-1.0 (dual-licensed)
- Orion's choice: CDDL-1.0 (decision #43)
- Link mode: Dynamic (`libraw_r.dylib` in bundle)
- Licenses shipped: `data/lensfun/` has copies; app bundles LibRaw's LICENSE.LGPL, LICENSE.CDDL, COPYRIGHT
- Proprietary: **Yes** — CDDL-1.0 is file-based copyleft and permits proprietary combination so long as CDDL files stay under CDDL

**OpenCV** (engine/CMakeLists.txt:77–102)  
- Upstream license: Apache-2.0 (verified in package-app.sh comments)
- Link mode: Dynamic (five `.dylib`s: core, imgproc, features, geometry, video)
- Proprietary: **Yes** — Apache-2.0 permits proprietary combination

### System frameworks (app/CMakeLists.txt:137–139)

Metal, MetalKit, Foundation, CoreGraphics, ImageIO, AppKit, Vision, CoreVideo — all Apple proprietary frameworks. Linked as `-framework` (system libraries). No third-party license applies.

**SQLite3** (app/PhotoIndex.swift, imported as `import SQLite3`)  
- License: Public domain (sqlite.org/copyright)
- Link mode: Swift framework import (system library on macOS)
- Proprietary: **Yes** — public domain

### Data assets

**Lensfun camera/lens database** (data/lensfun/)  
- License: CC BY-SA 3.0 (verified: `data/lensfun/COPYING.CC_BY-SA_3.0`)
- Bundled as: `Contents/Resources/data/lensfun/` (~1,450 lens calibrations in XML)
- Proprietary: **Conditional** — CC BY-SA 3.0 binds the *data*, not the application. Orion's app code can be proprietary. However, if any lens entry is corrected or extended, that correction must be offered under CC BY-SA. The practical effect: **fixes to lens calibrations must be contributed back** (already the right outcome per NOTICE section).

### Build-time only

**Slang** (third_party/slang/LICENSE)  
- License: Apache-2.0 WITH LLVM-exception  
- Usage: Shader compiler (`slangc` binary in build phase, engine/shaders/CMakeLists.txt:13)
- Shipped in binary: **No** — compiler runs at build time; only compiled Metal `.metallib` files are in the app
- Proprietary: **Yes** — build tools are not subject to runtime licenses

### Algorithms from published descriptions (per NOTICE)

NOTICE states these are implemented from published research papers, not ported from copyrighted source code. No new license obligations arise. See `research/` directory for citations.

---

## Transitive dependencies

OpenCV and LibRaw pull in other Homebrew packages at link time. All are dynamically linked and bundled in `Contents/Frameworks/`:

| Library | License | Upstream | Proprietary |
|---------|---------|----------|------------|
| libomp | Apache-2.0 + LLVM-exception | LLVM project | Yes |
| libjpeg-turbo | JPEG/BSD-like | Mozilla | Yes |
| liblcms2 | MIT | Marti Maria | Yes |
| libprotobuf | BSD-3-Clause | Google | Yes |
| libpugixml | MIT | Arseny Kapoulkine | Yes |
| libtbb | Apache-2.0 | Intel | Yes |
| libopenblas | BSD-3-Clause | Kazushige Goto et al. | Yes |
| libabsl (Abseil) | Apache-2.0 | Google | Yes |
| libopenvino | Apache-2.0 | Intel | Yes |
| libgfortran / libgcc_s / libquadmath | GPL + runtime exception | GCC | Yes (runtime exception) |

All dynamically linked. No GPL source code is compiled; only the runtime exception applies. No source code shipping required.

---

## Verification

**Checked files:**
- `CMakeLists.txt`, `engine/CMakeLists.txt`, `app/CMakeLists.txt` (dependencies declared)
- `NOTICE` (third-party notices as of transition)
- `third_party/slang/LICENSE` (Slang license verified)
- `data/lensfun/COPYING.CC_BY-SA_3.0` (Lensfun license verified)
- `tools/package-app.sh:229–255` (licenses bundled at ship time)
- `dist/Orion.app/Contents/Frameworks/` (actual bundled dylibs listed)
- `planning/DECISIONS.md` #43–45 (licensing decisions and open questions)

**Not verified in tree:** OpenColorIO is documented as a planned stack component but not found in code or build.
lcms2 is linked directly off macOS since 2026-11-07 (`engine/src/util/ExportColour.cpp`); on macOS it stays a transitive dependency of OpenCV.

**Checked 2026-09-13 at commit: abf3a51**

---

## Export metadata and colour off macOS (2026-11-07)

| Need | Chosen | Licence | Rejected, and why |
|---|---|---|---|
| ICC profiles and conversion | Little CMS 2 (`lcms`, vcpkg) | MIT | none needed: already shipped as libjxl's and OpenCV's dependency |
| Reading the camera's EXIF | LibRaw, already the decoder | CDDL-1.0 (chosen) | exiv2: GPL-2.0+, never read or linked (#10) |
| Writing EXIF and the TIFF | Orion's own directory writer, `engine/src/util/TiffIfd.cpp`, shared with the DNG writer, written from TIFF 6.0 and CIPA DC-008 (Exif 2.32) | Orion's | libexif: LGPL-2.1+, would be the first LGPL library shipped (LibRaw is taken under CDDL) with its source-offer duty, for ten tags; TinyEXIF and easyexif: read only; libtiff's EXIF directory writing needs a write, reload and rewrite of IFD0 its own test calls fragile, and OpenCV's TIFF encoder takes no metadata |
| Writing XMP | a packet built as text in `ExportMetadata.cpp` | Orion's | an XMP toolkit: Adobe's XMP SDK is BSD but is a large C++ dependency for one flat packet |

The Windows and Linux packages carry no new library: Little CMS, libtiff and zlib were already in them (`NOTICE`).
zlib is linked directly by the engine off macOS, to deflate the TIFF export's strips; libtiff only by `orion-tests`, as the second reader of the exported TIFF.

---

## macOS: what OpenCV 5.1 brings (2026-11-20)

Homebrew's `opencv` 5.1.0 links two things 5.0.0 did not: `imgproc` loads HarfBuzz, and `features`, `video` and `dnn` load `imgcodecs`.
Those edges are plain `LC_LOAD_DYLIB` of modules the shim links, so only a different OpenCV build removes them.
Below them it is Homebrew's builds that decide: GLib, libintl and PCRE2 arrive only because Homebrew's HarfBuzz is built with GLib (`libharfbuzz` exports `hb_glib_*` and imports `g_unichar_*`), while `imgproc` imports 39 `hb_*` functions and no `hb_glib_*` or `g_*`.
jpeg-xl 0.13.0 adds nothing: libjxl, libjxl_threads and libjxl_cms link what 0.12.0 did.
Licences verified against each bottle's installed file and the upstream file at the same tag (identical byte for byte), and for libintl against gettext 1.0's `gettext-runtime/COPYING`.
What each library compiles in was checked by one pass, run the same way over all 20 and recorded per library below. For each: the source release Homebrew built (sha256 as the formula gives; libaom from googlesource at the formula's revision), its licence files and third-party directories; a keyword scan of every library source file (tests, tools, examples and other architectures left out) for licence statements and copyright lines, listing each holder its copied licence file does not name; and, for each file that turned up, whether Homebrew's arm64 build compiles it (symbols or code constants in the bottle, or the build file). The scan matches keywords; it is not a reading of every file.
Versions: libavif, aom, dav1d, libvmaf, openjpeg, openexr, imath, libdeflate and openjph were poured by the failing run (its log); the other eleven came with the runner image, so their versions are the formulae API's on 2026-11-20, not observed.

| Library: dylibs | Homebrew | Licence (SPDX), option taken | Obligation | Reached by |
|---|---|---|---|---|
| HarfBuzz: libharfbuzz, libharfbuzz-raster | harfbuzz 15.1.0 | libharfbuzz: MIT-Modern-Variant ("Old MIT") AND ISC (`hb-ucd.cc`) AND MIT (fasthash, `hb-algs.hh`); USE and vowel-constraint tables from Microsoft data (MIT); emoji tables from Unicode data. libharfbuzz-raster: MIT-Modern-Variant (no UCD symbols, no fasthash constants) | notice + text; the others in NOTICE | imgproc |
| FreeType: libfreetype | freetype 2.15.0 | FTL OR GPL-2.0-or-later: **FTL**; BDF and PCF drivers and `fthash.c` under MIT-style notices; `pcfutil.c` MIT-open-group; Adobe's CFF engine FTL with a contributor patent licence | FTL credit, the BDF/PCF/fthash notices and The Open Group's in NOTICE | HarfBuzz |
| Graphite2: libgraphite2 | graphite2 1.3.15 | MIT OR MPL-2.0 OR LGPL-2.1-or-later OR GPL-2.0-or-later: **MIT** | notice + text | HarfBuzz |
| GLib: libglib-2.0 | glib 2.90.1 | **LGPL-2.1-or-later** (only option); `valgrind.h` BSD-style, no binary condition | see below | HarfBuzz |
| GNU libintl: libintl | gettext 1.0 | **LGPL-2.1-or-later** (only option); its Bison parser (`plural.c`, `plural.h`) is GPL-3.0-or-later WITH Bison-exception-2.2, which lets a larger work containing the skeleton take any terms "so long as that work isn't itself a parser generator using the skeleton or a modified version thereof as a parser skeleton"; `alloca.h`, see below | see below | GLib |
| PCRE2: libpcre2-8 | pcre2 10.49 | BSD-3-Clause WITH PCRE2-exception AND BSD-2-Clause (sljit, the JIT, built) | notice + text; sljit's notice in NOTICE | GLib |
| libavif: libavif | libavif 1.4.2 | BSD-2-Clause, with libyuv's scaler (BSD-3-Clause) compiled in | notice + text (its LICENSE carries libyuv's); three added holders in NOTICE | imgcodecs |
| libaom: libaom | aom 3.15.3 | BSD-2-Clause AND BSD-3-Clause (fastfeat) AND MIT (vector); libyuv is not in Homebrew's build | notice + text, fastfeat's and the WebM authors' in NOTICE; AOM Patent License 1.0 reproduced with the binary and the licensee's own Necessary Claims made available under it (conditions of its grant) | libavif |
| dav1d: libdav1d | dav1d 1.5.5 | BSD-2-Clause | notice + text; five added holders in NOTICE | libavif |
| libvmaf: libvmaf | libvmaf 3.2.1 | BSD-2-Clause-Patent AND BSD-3-Clause (LIBSVM; Tom Distler's IQA) AND BSD-2-Clause (Xiph's PSNR-HVS) AND MIT (CIEDE2000 from av-metrics; mkdirp, "MIT licensed" with no text) | notice + text, the others in NOTICE; patent grant in the licence | libaom |
| libwebp: libwebp, libwebpmux, libwebpdemux, libsharpyuv | webp 1.6.0 | BSD-3-Clause; Google's additional patent grant (upstream PATENTS, no reproduction condition) | notice + text | imgcodecs, LibTIFF |
| libpng: libpng16 | libpng 1.6.59 | libpng-2.0 (v2, and v1 for 0.5 to 1.6.35 code, in one file) | notice + text; Arm Holdings' line in NOTICE | imgcodecs, FreeType, libharfbuzz-raster |
| LibTIFF: libtiff | libtiff 4.7.2 | libtiff (LZW code under a Berkeley notice in the same file; codec files add holders under the same terms, PixarLog and LogLuv with notices that also name Pixar and Greg Larson; the fax decoder derived from Frank Cringle's viewfax with permission) AND MIT (`tif_hash_set.c`) | notice + text; the added holders and the two codec notices in NOTICE | imgcodecs |
| OpenJPEG: libopenjp2 | openjpeg 2.5.4_1 | BSD-2-Clause | notice + text; fourteen added holders in NOTICE | imgcodecs |
| OpenEXR: libOpenEXR, libOpenEXRCore, libIex, libIlmThread | openexr 3.5.4 | BSD-3-Clause (its `external/` copies are not built: deflate, zstd and OpenJPH are linked) | notice + text; four added holders in NOTICE | imgcodecs |
| Imath: libImath | imath 3.2.3 | BSD-3-Clause | notice + text | imgcodecs, OpenEXR |
| OpenJPH: libopenjph | openjph 0.33.0 | BSD-2-Clause | notice + text; one added holder in NOTICE | OpenEXR |
| libdeflate: libdeflate | libdeflate 1.26 | MIT | notice + text | OpenEXR |
| XZ Utils: liblzma | xz 5.8.4 | 0BSD (every SPDX header in `src/liblzma` and `src/common`; Homebrew's GPL-2.0-or-later is the xzgrep scripts, not shipped) | none | LibTIFF |
| Zstandard: libzstd | zstd 1.5.7_1 | (BSD-3-Clause OR GPL-2.0-only) AND BSD-2-Clause AND MIT: **BSD-3-Clause**; BSD-2-Clause is the legacy decoders v0.1 to v0.7 (built: `-DZSTD_LEGACY_SUPPORT=ON`, 91 `ZSTDv0*` symbols), MIT is libdivsufsort-lite | notice + text; the BSD-2 and MIT notices and three added holders in NOTICE | OpenEXR, LibTIFF |

What the pass found, per library (a holder is "added" when a compiled file names it and the copied licence file does not):

| Library | Found, and whether built |
|---|---|
| HarfBuzz | of 491 `src/` files, 398 Old MIT and 91 with no header (under COPYING); `hb-ucd.cc` ISC (built, 20 UCD symbols); `hb-algs.hh` fasthash MIT (built: its constants are in libharfbuzz's code, not in libharfbuzz-raster's); added Elie Roux (BASE table, built); Skef Iterum's line is only in `colrv1-depend.hh`, included only by `hb-depend.cc`, which is libharfbuzz-subset's (not shipped) |
| FreeType | 450 of 493 files under the FreeType project licence, 21 of them Adobe's CFF engine with its patent licence (built); BDF and PCF drivers (built) with wider years than their READMEs; `fthash.c` (built, 11 `ft_hash` symbols); `pcfutil.c`, The Open Group (built, included by `pcf.c`); `dlg` BSL-1.0, `md5.c` (debug builds only), the bundled zlib (system zlib is linked) and the HarfBuzz autofit glue not built |
| Graphite2 | 71 of 71 files the same four-way SPDX expression, SIL International only |
| GLib | `glib/` LGPL-2.1-or-later; MIT user-directory code in `gutils.c:2254-2276`, unix branch, not built on macOS (`HAVE_COCOA` selects `load_user_special_dirs_macos`, which the bottle has, and it has no `user-dirs.dirs` string); `valgrind.h` BSD-style without a binary condition; `win_iconv.c` included only under `G_OS_WIN32`; generated Unicode tables (`gunichartables.h`, `gunidecomp.h`, `gscripttable.h`, `gmirroringtable.h`) name no terms |
| libintl | 196 of 200 files LGPL; `plural.c`/`plural.h` Bison output; `gnulib-lib/alloca.in.h` GPL-3.0-or-later, see below |
| PCRE2 | 78 of 79 `src/` files BSD, University of Cambridge; sljit's 33 files BSD-2-Clause, Zoltan Herczeg (built); `pcre2_ucd.c` generated from Unicode data, names no terms |
| libavif | BSD-2-Clause; libyuv's scaler (built, 11 `ScalePlane` symbols); added Google LLC, Brad Hards, Two Orioles (`obu.c`); AVM, SVT, libgav1 and rav1e glue not built |
| libaom | BSD-2-Clause with the AOM patent licence; fastfeat and vector built; added the WebM project authors (12 Arm NEON files, built); libyuv, highway and `common/y4minput` not in the library |
| dav1d | 155 of 156 BSD-2-Clause; added Two Orioles, Martin Storsjo, Janne Grunau, Niklas Haas, Arm Limited (arm64 and generic files, built); x86, ppc, loongarch, riscv and arm32 files not built |
| libvmaf | BSD-2-Clause-Patent; LIBSVM, Xiph PSNR-HVS, Tom Distler's IQA (`__iqa_convolve`, `_compute_ssim`, `_compute_ms_ssim`), CIEDE2000 port (`_vmaf_fex_ciede`), mkdirp (`_mkdirp`) all built; `integer_ssim.c`, CUDA, x86 and `compat/` not built |
| libwebp | 176 of 176 files Google Inc, BSD-3-Clause |
| libpng | 25 of 25 files the libpng licence; added Arm Holdings (`arm/palette_neon_intrinsics.c`, built) |
| LibTIFF | 45 of 53 files the libtiff licence; LZW Berkeley notice (in LICENSE.md); added Kiselev, Planet Labs, Mapbox, Even Rouault, Joris Van Damme and AWare (built); Cringle's viewfax derivation (built); PixarLog and LogLuv own clauses (built); hash set MIT (built); LERC and JBIG not built |
| OpenJPEG | 57 of 59 BSD-2-Clause; fourteen added holders, each in a built file; JPIP index code not built |
| OpenEXR | 371 of 372 `src/lib` files BSD-3-Clause; added DreamWorks, Weta, Autodesk, Pixar Animation Studios (built); `external/` not built |
| Imath | 41 of 41 files BSD-3-Clause, Contributors to the OpenEXR Project |
| libdeflate | MIT; Eric Biggers is the only holder its files name; one file notes code placed in the public domain by Igor Pavlov |
| OpenJPH | BSD-2-Clause; added Osamu Watanabe (built); Intel's AVX2/AVX-512 files not in the arm64 build (0 AVX symbols) |
| XZ Utils | every `.c` and `.h` file in `src/liblzma` and `src/common` (172) 0BSD |
| Zstandard | dual BSD/GPL-2.0; legacy decoders' 66 identical BSD-2 sections (built, all of v0.1 to v0.7); divsufsort MIT (built); added Yann Collet's two Meta lines and Tino Reichardt (built) |

No library here is GPL-only. No GPL text is copied: FreeType's, Graphite2's and Zstandard's GPL options are not taken, and gettext's GPL-3.0 `COPYING` is not shipped.

**Open: libintl's `alloca.h`.** gettext 1.0 ships its own `gnulib-local/lib/alloca.in.h`, licensed GPL-3.0-or-later; libintl's `gnulib-lib/alloca.in.h` is the same file. On macOS `intl/gnulib-m4/alloca.m4` (lines 17-39) finds a working `<alloca.h>` and a compiler with `__GNUC__`, so the build generates `alloca.h` from it, and `-Ignulib-lib` puts it first on the include path. `gnulib-lib/vasnprintf.c` includes it unconditionally, and `printf.c` compiles `vasnprintf.c` into itself (`#define vasnprintf _libintl_vasnprintf`, then `#include "vasnprintf.c"`); Homebrew's libintl has `__libintl_vasnprintf` and `__libintl_printf_parse`. `dcigettext.c`, `localealias.c`, `printf.c` and `plural.c` include `<alloca.h>` directly only without `__GNUC__`, which clang defines. Under clang the header reduces to `#define alloca __builtin_alloca`.
- For "no GPL obligation": the header is a macro naming a compiler built-in, and no function or data from the file reaches the binary. gnulib's own `lib/alloca.in.h` is LGPL-2.1-or-later (module `alloca-opt`, "LGPLv2+", at gnulib's current master), so the GPL-3.0 on gettext's copy is gettext's packaging, not gnulib's terms. gettext states libintl is under the LGPL (`gettext-runtime/COPYING`, `intl/COPYING.LIB`).
- Against: the file compiled into libintl says GPL-3.0-or-later and nothing else. Read strictly, libintl contains GPL-3.0 material, and Orion cannot carry GPL code, linked dynamically or not (#359; rule 3 of `tools/package-gates.py`).
Not decided here; it is the developer's call, and it goes away with GLib.

**Unicode data.** HarfBuzz, GLib and PCRE2 carry tables generated from Unicode's data files (HarfBuzz's USE and vowel-constraint tables also from Microsoft's `ms-use` files). Only HarfBuzz's emoji table names Unicode's terms (in NOTICE). The Unicode License v3 text travels only in the Linux package, as ICU's `icu.txt`. Whether it or Unicode's terms of use is owed for these tables on macOS: not decided here.

**LGPL-2.1-or-later, GLib and libintl.** The first LGPL libraries shipped since libquadmath (whose row in the summary table above files it under GCC's runtime exception; NOTICE has it as LGPL-2.1).
What distributing them requires, under LGPL-2.1:

| Section | Requirement | State in this tree |
|---|---|---|
| 6, 6(b) | a separate shared library the user can replace with an interface-compatible build | met: separate dylibs, GLib loaded by HarfBuzz and libintl by GLib; a replaced file needs the bundle re-signed (ad-hoc today) |
| 6 | prominent notice that the library is used and is under the LGPL, with a copy of the licence | met: NOTICE, and `LGPL-2.1-or-later.txt` from GLib's package |
| 6 | "If the work during execution displays copyright notices", the library's among them, with a reference to the licence | **unverified**: `git grep` finds no copyright display in `shell/`, `shell/` sets no `NativeMenu`, and `Info.plist` sets no `NSHumanReadableCopyright`. But Avalonia.Native 12.1.4 carries an "About Avalonia" menu string and an `AboutAvaloniaDialog`, and whether Orion's macOS app menu shows it, and what it displays, was not checked by running the app |
| 4 | the library's complete corresponding source with the binary, or equivalent access from the same place | **not met**: the GLib and gettext the runner has when it packages (2.90.1 and 1.0 on 2026-11-20, not observed in the log), with Homebrew's `hardcoded-paths.diff` for GLib and both formulae; nothing in the tree offers them (nor libquadmath's) |
| 6 | the terms "permit modification of the work for the customer's own use and reverse engineering for debugging such modifications" | open: Orion's `LICENSE` permits changes for permitted purposes and is silent on reverse engineering |

Not decided here: whether to ship them, and how to meet section 4.

---

## Open question (inherited from #45)

LibRaw's `cam_xyz` colour matrices may be Adobe-derived (documented in planning/DECISIONS.md #45). If so, Orion ships Adobe-derived colour data — the same position as darktable, RawTherapee and every dcraw descendant. **This does not block proprietary distribution** (colour data is part of the open-source tradition for RAW editors), but it is documented here as context. A primary statement from dcraw's author was not found.

---

## Recommendations for going closed-source

1. **Keep NOTICE file.** Apache-2.0 §4(d) requires it to travel with the binary. Display it in About/Settings.
2. **License files in bundle.** Already done by `package-app.sh`: copies to `Contents/Resources/licenses/`.
3. **No changes needed for LibRaw.** CDDL-1.0 choice enables static or dynamic linking in proprietary code. Current dynamic linking is compliant.
4. **Lensfun fixes must stay CC BY-SA.** If any lens calibration is improved, contribute it back to lensfun project.
5. **Mark the transition clearly.** Current NOTICE states "Orion is proprietary… Revisions up to commit abf3a51 remain available under Apache License, Version 2.0." This is correct and should stay.

---

## Unclear points marked as such

No blocked findings. All components permit closed-source distribution.
