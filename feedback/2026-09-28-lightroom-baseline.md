# Lightroom comparison baseline

2026-09-28. Read-only research for the whole-product audit. No Lightroom or Orion
benchmark, app launch, GPU run, RAW decode, or memory stress test was performed.
**No claim that Orion matches or beats Lightroom Classic is supported yet.**

## Scope and measurement rule

Compare the *same photographer action* on the same Mac, display, RAWs, storage,
power mode, and warm/cold state. Record macOS and app versions, display refresh,
Lightroom's GPU and preview settings, XMP auto-write setting, Orion build, and
whether the Adobe Camera Raw/preview caches are warm. Alternate app order and
report median and p95 over repeated trials, with a screen recording for the
visible result and Instruments traces for unexplained stalls. Report both time
to first **correct edited** image and time to final settled image; an embedded
camera JPEG or stale prior frame is not the former. Adobe explicitly says
embedded previews may differ from processed previews [A1], so visual quality
and speed must be judged at the same state.

Lightroom Classic uses an import/catalog record even when **Add** leaves originals
in place [A2]; Orion's settled design is folder-first, XMP as truth, SQLite as a
disposable index (decision #9). Compare open-folder-to-useful-grid and subsequent
browse behavior, but **do not require a catalog or a catalog migration**. Adobe
documents standard/1:1 previews, a preview cache limit, Camera Raw cache and GPU
preview generation [A1, A3, A4]. These explain benchmark controls, not Lightroom
latency, RAM, or undocumented rendering internals.

| Workflow | Observable acceptance for Orion | Measure / present evidence |
|---|---|---|
| Continuous edit: exposure, WB, local mask, 100% pan | Each input produces the correct preview without a wrong-photo/wrong-look flash; settle agrees with full render. Retain Orion's existing **<16 ms preview-feedback target** as an internal target, and separately measure pointer event → displayed pixel. | Median/p95 input-to-photon, dropped frames/hitches, release → final settle, image hashes or pixel comparisons. Apple says one refresh interval is roughly 8–16 ms and main-thread continuous work should be shorter [P1, P2]. The current 42 MP exposure **17.38 ms p95** is render/work timing from #271, not an input-to-photon result. |
| Cold/warm open and photo switch | First displayed image belongs to selected photo; edited state arrives correctly, and no main-thread freeze blocks another action. | Select → first correct edited frame, final settle, longest main-thread stall; 24 and 42 MP, cold and warm, sidecar/no sidecar. Existing **210.9 ms** cold open (#151) is historical, not a current paired Lightroom result. |
| Folder browse, 5,000 RAWs | Correct listing/marks while switching folders or scrolling; abandoned folder work stops; memory reaches a repeatable plateau and falls to a bounded steady state after leaving. | Time to first useful grid, time until visible thumbnails complete, scroll hitch ratio, peak/steady **process footprint** and Metal texture bytes, after open/scroll/switch/close. No arbitrary GiB pass line before device and Lightroom measurements; current 9.51 GiB session retention (#271) and unbounded live thumbnails in the desktop audit make this a priority. Apple's Game Memory template separates VM footprint from Metal resource allocations [P3]. |
| Masks: create, brush, hide, reorder, undo, reopen | Selection and local adjustments remain attached to the intended photo/region through every operation. Mask overlay follows edited geometry; canceled/stale AI completion cannot change another photo. | Record interaction/AI inference latency and settle separately, footprint before/after eight masks and long brush, plus before/after rendered pixels and reopened sidecar/matte files. Compare shared mask kinds and operations only. Adobe documents non-destructive local masking and mask overlays [A5]; the masking audit identifies correctness gaps that block a speed win. |
| Edit/rate/sync across files | A saved edit is durable after restart; a failed save remains owed; changing one photo never changes another; foreign XMP fields survive. A malformed existing payload remains untouched with a visible failure. | Byte-check RAW, XMP and matte siblings before/after switch, batch, failed write/retry, rating, and external-metadata round trip. Adobe documents optional automatic XMP writes and newer ACR companions for heavy edits [A6, A7]. Orion need not copy that storage split; its own XMP-truth invariant and interoperability matter. |
| Export and cancel | Exported pixels match the chosen saved edit; failed restore/export reports failure; Stop is actionable before the next photo and the UI keeps repainting. No partial destination is presented as a finished export. | Time per photo/whole batch, main-queue heartbeat and Stop → last new job, output validity, sidecar byte equality, failure/cancel counts. The desktop audit traces a synchronous batch loop and cross-photo autosave corruption; these are correctness gates before throughput ranking. |

**Comparison threshold:** first fix the correctness failures in the masking and
desktop-I/O audits. Then publish paired Orion/Lightroom Classic numbers for
each applicable row, including confidence/spread and settings. “Better” means
lower p95 input-to-correct-display and settle time on the shared workflow **with
no worse correctness or materially higher peak/retained footprint**; do not
collapse unlike workflows into one score. Apple places noticeable discrete
interaction delay around 50–100 ms and advises keeping non-UI work off the main
thread [P1, P2]. Those are platform guidance, not published Lightroom results.

## Evidence still needed

1. A pinned same-machine Lightroom Classic installation/version and Orion build,
   shared 24/42 MP and 5,000-file fixtures, source-drive class, preview/cache/XMP
   settings, display rate, and paired cold/warm run log. No such competitor run
   appears in the current audits.
2. Screen-timestamped input → **correct edited pixel** and settle traces, plus
   Instruments Hangs/Hitches, CPU, Game Memory/VM Tracker, and Metal resource
   captures. Existing engine timings and arithmetic thumbnail estimates are not
   process footprint or end-to-end UX measurements [P1, P3, P4].
3. Render/file regression evidence for every P1 in
   `2026-09-28-masking-audit.md` and `2026-09-28-desktop-io-audit.md`, followed by
   the same benchmark after fixes. No nine-gate or physical-gesture/VoiceOver
   result is implied by this research note.

## Primary sources

- [A1 Adobe: Optimize Lightroom performance](https://helpx.adobe.com/lightroom-classic/desktop/technical-support/performance-guidelines/optimize-performance-lightroom.html) — preview types, embedded/processed look differences, Camera Raw cache.
- [A2 Adobe: Import photos from a folder](https://helpx.adobe.com/lightroom-classic/desktop/import-photos/import-photos-video-catalog.html) — catalog link and Add/Copy/Move behavior.
- [A3 Adobe: Catalog and preview cache settings](https://helpx.adobe.com/lightroom-classic/desktop/manage-catalogs-and-files/create-catalogs.html) — standard and 1:1 previews, cache limit/discard.
- [A4 Adobe: GPU preview generation](https://helpx.adobe.com/lightroom-classic/desktop/kb/gpu-preview-generation.html) — Auto/On/Off control.
- [A5 Adobe: Masking tool](https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/masking.html) — mask kinds, overlay, non-destructive local edits.
- [A6 Adobe: Advanced metadata actions](https://helpx.adobe.com/lightroom-classic/desktop/organize-photos-in-lightroom-classic/advanced-metadata-actions.html) — XMP writes and automatic-write option.
- [A7 Adobe: Save metadata to external sidecars](https://helpx.adobe.com/lightroom-classic/desktop/organize-photos-in-lightroom-classic/create-xmp-acr-files.html) — XMP and Lightroom Classic 15 ACR sidecars.
- [P1 Apple: Improving app responsiveness](https://developer.apple.com/documentation/xcode/improving-app-responsiveness) — interaction, main-thread and frame guidance.
- [P2 Apple: Understanding UI responsiveness](https://developer.apple.com/documentation/xcode/understanding-user-interface-responsiveness/) — hangs versus hitches.
- [P3 Apple: Analyzing Metal app memory](https://developer.apple.com/documentation/xcode/analyzing-the-memory-usage-of-your-metal-app) — Game Memory, VM Tracker, resource events and footprint.
- [P4 Apple: Analyzing Metal app performance](https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app) — display-time and skipped-vsync evidence.
