# LICENSE AUDIT — Orion RAW Editor

**Date:** 2026-09-13  
**Purpose:** Verify proprietary binary distribution compatibility  
**Transition:** From Apache-2.0 (commit abf3a51) to closed-source  

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

**Not verified in tree:** OpenColorIO and lcms2 are documented as planned stack components but not found in code or build. They are pulled in as transitive dependencies of OpenCV but not directly referenced.

**Checked 2026-09-13 at commit: abf3a51**

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
