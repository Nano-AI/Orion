# Build and packaging source audit — 2026-09-28

Complete source read of 12 first-party build/tooling files (1,265 lines) at
`23a242b`. No packaging script, worktree setup, compiler, build or application
was executed. These are source-derived findings, not measured build timings or
an installed-system compatibility test. The previous macOS dependency warning
is recorded separately in `2026-09-28-verification.md`.

| Priority | Finding and concrete trigger | Minimum correction and missing proof |
|---|---|---|
| P1 | `tools/package-app.sh:35,49` accepts an arbitrary output directory, then recursively removes that entire directory. With a built app present, passing an existing export/archive directory deletes unrelated contents before staging. No ownership or containment check precedes removal. | Stage in a new owned temporary directory and replace only the script's named outputs, or refuse a nonempty destination. Pin refusal with a temporary directory containing a sentinel; never test against real photos or the checkout. |
| P2 | `tools/worktree-setup.sh:83-85` copies the text of a RAW symlink into the new worktree without making a relative target absolute. A valid main-checkout link such as `samples/A.ARW -> ../fixtures/A.ARW` resolves relative to the new `samples/` afterward and can be broken or point at another file. The comment promises resolution but `readlink` only reads one link. | Resolve the original file to its canonical absolute target before creating the new link. A temporary two-directory fixture should assert equal final targets for regular files, absolute links and relative/chained links. Existing guards against replacing a real sample directory should remain. |
| P2 | The package claims macOS 14 support (`tools/package-app.sh:350`) but its checks cover signatures, library paths and personal data only (`:269-327`). It does not reject a copied dylib whose minimum OS exceeds 14. Prior actual builds warned that installed LibRaw/OpenCV target macOS 26; a successful package run therefore cannot establish the advertised floor. | Inspect the minimum OS of every bundled Mach-O and refuse a dependency above the declared floor; rebuild dependencies for the supported floor. Keep an actual macOS 14 launch/open/export check separate. No older OS was available or tested here. |
| P3 | Every shader translation depends on every shader source (`engine/shaders/CMakeLists.txt:85-106`). Editing one independent leaf kernel invalidates all entries in `ORION_SHADERS`, even when their include graph is unchanged. This adds avoidable compiler work during iteration. | First verify Slang's dependency-output support; use it if available, otherwise narrow dependencies using the existing include structure. Preserve shared-include invalidation. A dry-run dependency check must show one leaf edit stays local and a shared include still rebuilds its users; no timing or resource saving is claimed yet. |

The packaging dependency traversal already marks libraries seen at enqueue,
avoiding its documented earlier queue explosion (#218). This pass does not
propose removing OpenCV modules: that tradeoff was explicitly settled there.
Worktree setup already isolates sidecars and refuses the main checkout; the
relative-link finding concerns only the RAW target it installs.

| Full file read | Lines | Full file read | Lines |
|---|---:|---|---:|
| `CMakeLists.txt` | 38 | `engine/CMakeLists.txt` | 113 |
| `engine/shaders/CMakeLists.txt` | 132 | `app/CMakeLists.txt` | 384 |
| `apps/bench/CMakeLists.txt` | 25 | `apps/tests/CMakeLists.txt` | 48 |
| `apps/pixstat/CMakeLists.txt` | 16 | `apps/probe/CMakeLists.txt` | 3 |
| `apps/rawstat/CMakeLists.txt` | 4 | `tools/package-app.sh` | 366 |
| `tools/worktree-setup.sh` | 91 | `.github/workflows/pages.yml` | 45 |

Relevant decision rows #4, #26, #218 and #266, packaging caller mentions, and
the prior verification report were targeted supporting reads. No product-source
coverage count is increased by these 12 tooling files. Test/bench source and
remaining checker scripts still need their own complete reads.
