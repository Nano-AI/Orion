# Orion — Research Notes

Condensed findings. Quality ▲ high / ■ mid / ▽ low · Speed F fast / M medium / S slow.

---

## 1. Demosaicing, Denoising, Highlights

**What the references ship:** darktable defaults to **RCD** (Bayer) + Markesteijn 1-pass (X-Trans). RawTherapee/ART default to **AMaZE**. vkdt runs everything as Vulkan compute, incl. a joint denoise+demosaic U-Net.

⚠ **Markesteijn has never been published** — checked 2026-08-02, decision #114. Both projects' copies are GPL-3 and the only description of the algorithm is the code itself, so it cannot be reimplemented from a paper. LibRaw's core carries the same code under **LGPL-2.1/CDDL-1.0**, which is the route. `research/demosaic-xtrans.md`.

### Demosaic
| Algorithm | Quality | Speed | GPU fit | Notes |
|---|---|---|---|---|
| Bilinear / half-size | ▽ | F | Excellent | Thumbnails, zoomed-out preview |
| PPG | ▽–■ | F | Good | darktable's former default |
| **RCD** | ▲ | F–M | **Excellent** | Near-AMaZE detail, fewer overshoots. [MIT reference impl](https://github.com/LuisSR/RCD-Demosaicing) |
| LMMSE | ▲ at high ISO | M | Poor | Best on noisy raws; dt has no OpenCL path |
| AMaZE | ▲ | S | Poor | Best fine detail but branchy/serial, GPL-3 from RawTherapee |
| Dual demosaic (RCD+VNG4) | ▲ | S | Good | Detail algo on texture, smooth algo on sky, blended |
| ML joint denoise+demosaic | ▲+ | S–M | Excellent | [vkdt-nn](https://codeberg.org/hanatos/vkdt-nn) compiles ONNX→SPIR-V; [Gharbi 2016](https://www.researchgate.net/publication/309958252_Deep_joint_demosaicking_and_denoising) |

Docs: [darktable demosaic](https://docs.darktable.org/usermanual/4.6/en/module-reference/processing-modules/demosaic/) · [RawPedia](https://rawpedia.rawtherapee.com/Demosaicing)

### Denoise
| Method | Quality | Speed | GPU fit | Notes |
|---|---|---|---|---|
| Wavelet (à-trous), profiled | ■ | F | Excellent | Per-camera/ISO noise model. [dt denoise-profiled](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/denoise-profiled/) |
| Non-local means, profiled | ■–▲ | M–S | Good | Better luma texture, resource-heavy |
| BM3D | ▲ | S | **Poor** | Block matching is GPU-hostile; GPU ports only 4–7× faster. [study](https://link.springer.com/article/10.1007/s11554-020-00945-4) — **skip** |
| OIDN-class small U-Net | ▲ | F–M | Excellent | Apache-2.0 architecture template. [openimagedenoise.org](https://www.openimagedenoise.org/) |
| **NAFNet** | ▲+ | M | Excellent | 40.30 dB SIDD @ 65 GMACs — best quality/compute ratio. [repo](https://github.com/megvii-research/NAFNet) |
| DeepPRIME-style joint raw NN | ▲++ | S | Good | DxO does ~2 MP/s → ~12 s for 24 MP. **Not interactive** — proves AI denoise must be a background pass |

### Highlight reconstruction
[darktable manual](https://docs.darktable.org/usermanual/4.6/en/module-reference/processing-modules/highlight-reconstruction/)
- **Clip** ▽ F — clamp to white, desaturates. Baseline.
- **Inpaint opposed** ■ F — estimate clipped channels from adjacent unclipped. dt's current default, GPU-friendly.
- **Segmentation based** ▲ M — reconstruct regions from surrounding color ratios. Best on large blown areas.
- **Guided laplacians** ▲ S — multi-scale guided filter, Bayer-only, needs GPU. [math writeup](https://ansel.photos/en/resources/guided-laplacian-highlights/)

### → Orion picks
- Ship: **RCD** (Bayer) + bilinear for thumbs. Profiled **wavelet** denoise + optional NLM luma.
- Highlights: **clip + inpaint-opposed** at launch, segmentation as quality option.
- Later, async not live: NAFNet-derived joint net via Core ML/ONNX, tiled, background "Enhance" button.
- Skip: BM3D entirely. Defer AMaZE, LMMSE, guided-laplacians.

---

## 2. Tone Mapping & Color Science

### Pipeline philosophy — scene-referred, non-negotiable
Display-referred (legacy, Lab-based) bakes in a display assumption early → artifacts and hue shifts when filtering nonlinear pixels. **Scene-referred** keeps data linear/unbounded until one final display transform. darktable switched; vkdt uses **linear Rec.2020 float, zero Lab code**, display transform is just a node near the end.
[dt scene-referred](https://docs.darktable.org/usermanual/3.6/en/overview/workflow/edit-scene-referred/) · [vkdt pipe design](https://github.com/hanatos/vkdt/blob/master/src/pipe/readme.md)

**Canonical order:** decode → WB → demosaic → camera matrix → working space → *linear ops* (exposure, denoise, grading, local contrast) → **one** display transform → display-space ops (creative LUT, output encode). Never stack two display transforms.

### Tone mappers
| | Verdict |
|---|---|
| filmic rgb | Very capable, notoriously fiddly multi-tab UI. Not for minimal app. |
| sigmoid | Pivots on middle gray, 2–3 sliders, film-like rolloff. [docs](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/sigmoid/) |
| **AgX** | Sigmoid in *inset* primaries → bright saturated colors desaturate to white instead of hue-skewing ("notorious six"). Blender 4.0 default, darktable 5.4 module. [sobotka/AgX](https://github.com/sobotka/AgX) · [explainer](https://avidandrew.com/agx-color.html) |
| ACES RRT | Cinema standard; 1.x has red→orange skews; 2.0 fixed but complex. **Bad fit.** |

### Camera profiles
Baseline = 3×3 camera→XYZ matrix, sourced from Adobe DNG Converter `ColorMatrix1/2` tags (dcraw `adobe_coeff` lineage). **DCP** is richer: dual-illuminant matrices + ForwardMatrix + HueSatMap LUTs + tone curve; preferred over ICC for raw input. RawTherapee ships a `dcpprofiles` folder + lets users drop in their own — good UX model. [RawPedia color mgmt](https://rawpedia.rawtherapee.com/Color_Management) · [dcamprof](https://torger.se/anders/dcamprof.html) (builds DCP from a ColorChecker shot)

### Grading math
- **Wheels:** ASC CDL `out = (in × slope + offset)^power` per channel + saturation. Applied on scene-linear. Note lift/gamma/gain ≠ CDL (lift pivots at white, offset shifts uniformly). [Pomfort deep dive](https://pomfort.com/article/an-in-depth-look-at-asc-cdl-based-color-controls/)
- **HSL mixer:** 8 hue bands, smooth per-band weighting, hue/sat/lum offset each. Lightroom Color Mixer = UX benchmark.
- **Split tone:** blend shadow/highlight tints weighted by luminance + balance pivot.
- **LUTs (.cube):** apply *after* tone map. **Tetrahedral** interpolation (4 taps) beats trilinear (8 taps) — smoother gradients, exact neutral gray, equal quality from ~20–25% smaller LUTs. Resolve/Adobe GPU paths use it. Trivial compute shader. [comparison](https://www.alestemple.net/blog/tetrahedral-vs-trilinear-lut-interpolation.html)

### Auto-enhance & local contrast — the "180° edit" machinery
- **Single-image exposure fusion** (HDR+ style): generate two synthetic exposures from one image, fuse. Biggest single ingredient of "phone photo pop" — shadow lift with preserved local contrast. [IPOL HDR+ analysis](https://www.ipol.im/pub/art/2021/336/article_lr.pdf) · [WACV 2020 single-image extension](https://openaccess.thecvf.com/content_WACV_2020/papers/Hessel_An_Extended_Exposure_Fusion_and_its_Application_to_Single_Image_WACV_2020_paper.pdf)
- **Local Laplacian filters** = gold standard halo-free clarity/texture. Fast approximation is ~50× faster (350 ms/MP single CPU core → trivially interactive on GPU), pure pyramid math. [Paris SIGGRAPH 2011](https://people.csail.mit.edu/sparis/publi/2011/siggraph/Paris_11_Local_Laplacian_Filters.pdf) · [Fast LLF](https://imagine.enpc.fr/~aubrym/projects/llf/texts/2014-fast-laplacian-filter.pdf)
- **Dehaze:** dark channel prior + guided-filter refinement. Local window ops, CUDA impls published. [reference](https://github.com/He-Zhang/image_dehaze)
- **Learned (v2):** HDRnet predicts local affine transforms in a bilateral grid from low-res input, applies at full res in real time on mobile GPU — right architecture if Orion ever ships neural auto. [paper](https://arxiv.org/abs/1707.02880)

### → Orion picks
- Scene-referred linear **Rec.2020 f16** pipeline, exactly one display transform, no Lab.
- Default tone mapper: **AgX-family sigmoid**, exposed as 2–3 sliders.
- Camera color: ship Adobe matrices as baseline, support user DCPs.
- **One-click "stunning" v1 = classical GPU stack:** percentile auto exposure/black point + single-image exposure fusion + fast local Laplacian clarity + dark-channel dehaze. Deterministic, fast, zero model weights.

---

## 3. Engine Architecture & GPU Pipelines

### Lessons from existing editors
- **darktable** — linear pixelpipe of modules, each written **twice** (C + OpenCL). Maintains 4 pipes: export (full quality), darkroom (ROI — only visible pixels), thumbnail, and a cut-down pipe skipping slow modules during mask/crop drags. Tiling splits oversized images into overlapping ROIs on low memory. *Cost:* dual implementations, constant CPU↔GPU transfers, darkroom/export mismatch. [pixelpipe](https://docs.darktable.org/usermanual/development/en/darkroom/pixelpipe/the-pixelpipe-and-module-order/)
- **vkdt** (darktable founder's rewrite) — generic multi-in/multi-out **DAG**, topologically sorted; **each node = exactly one compute shader**, all in a single command buffer. One large GPU allocation sub-divided for all buffers. Connectors default `rgba:f16`. GUI displays textures **while still on GPU** — zero readback. Result: real-time raw *video*. [pipe readme](https://github.com/hanatos/vkdt/blob/master/src/pipe/readme.md)
- **RawTherapee** — well-threaded but CPU-only; can't match GPU editors on slider feedback.

**Takeaway:** GPU-first, *one* implementation per op, DAG not chain, pixels never leave GPU, f16 working format, ROI + multi-resolution pipes from day one.

### Interactivity patterns (how to hit <16 ms)
1. Preview-resolution ROI pipe for editing; tiled full-res pipe only for export/100% zoom.
2. Per-node output caching → a slider dirties only its downstream subgraph.
3. Degrade-then-refine during drags (darktable's cut-down pipe), refine on idle.
4. Never round-trip to CPU. Display straight from the GPU texture.
5. LRU image cache + async thumbnail workers at app level.

Lightroom got its responsiveness the same way — moving the whole develop pipeline to GPU in Process Version 5+. [Adobe GPU FAQ](https://helpx.adobe.com/lightroom-classic/kb/lightroom-gpu-faq.html)

### UI patterns — Capture One vs Lightroom
- **Capture One:** tool-*tab* bar where each tab is a user-editable collection of tools. Add/remove/duplicate tools, custom tabs, pin tools above a scroll area, float tools, save/restore whole **workspaces**. Signature efficiency feature: **Speed Edit** (hold key + drag anywhere, no slider targeting). [UI overview](https://support.captureone.com/hc/en-us/articles/360002468797-User-interface-overview) · [customizing](https://support.captureone.com/hc/en-us/articles/8861094993949-Customizing-Capture-One)
- **Lightroom:** fixed modules, fixed panel order. Less flexible, more self-explanatory.
- The "professional" feel is mostly a **restrained neutral-gray dark theme** — neutral surround measurably improves color judgment and lets the photo dominate. [why neutral gray](https://photography.tutsplus.com/tutorials/want-better-color-use-neutral-gray-themes-in-adobe-photoshop-and-lightroom--cms-23342)

### Reference implementation to study
**RapidRAW** — a modern Lightroom-alike shipping today: GPU compute raw pipeline, ONNX subject/sky masking, <20 MB binary. Worth reading regardless of its language choice. [repo](https://github.com/CyberTimon/RapidRAW)

---

## 4. Academic Foundations (MIT CSAIL / Stanford / Google)

Nearly every technique that makes modern photo editing both fast *and* good traces to one MIT CSAIL group — Frédo Durand's, with Sylvain Paris, Jiawen Chen, Michaël Gharbi, Jonathan Ragan-Kelley — usually in collaboration with Adobe or Google. This is the single richest vein for Orion.

### ★ The big one: Bilateral Guided Upsampling
**Chen, Adams, Wadhwa, Hasinoff — SIGGRAPH Asia 2016.** Run an expensive operator on a *low-resolution* copy, then model what it did by fitting a 3D grid of local affine color transforms in a bilateral grid, and evaluate those cheap curves on the full-resolution image. Faithfully reproduces tone mapping, style transfer, and recoloring operators.
[paper](https://www.semanticscholar.org/paper/Bilateral-guided-upsampling-Chen-Adams/c03a821d60dfa6616ecf12c3a6d65988e69f789e) · [ACM](https://dl.acm.org/doi/10.1145/2980179.2982423) · **[google/bgu — open source, MATLAB + Halide](https://github.com/google/bgu)**

**Why this matters more than any single feature:** it is a *general escape hatch* for the 16 ms budget. Any operator too slow to run live — local Laplacian, exposure fusion, ML denoise, even a future neural enhance — can run at 1/16 resolution and still produce a full-res result that's interactive. It decouples "how expensive is this algorithm" from "how fast does the slider feel." Worth building into the DAG as a node type, not bolting on later.

### Bilateral grid — the substructure
**Chen, Paris, Durand — MIT CSAIL, SIGGRAPH 2007.** Turns edge-aware operations (bilateral filtering, local histogram equalization, edge-aware brushing) into *local, independent* manipulations that parallelize cleanly. The authors hit real-time on HD video on 2007-era GPUs.
[paper PDF](https://groups.csail.mit.edu/graphics/bilagrid/siggraph07/RTEAIPBG.pdf) · [ACM](https://dl.acm.org/doi/10.1145/1276377.1276506) · [SIGGRAPH retrospective](https://history.siggraph.org/learning/real-time-edge-aware-image-processing-with-the-bilateral-grid-by-chen-paris-and-durand/)

Directly useful for: range masks, luminance masks, edge-aware local contrast, and the brush's edge-snapping behavior.

### Local Laplacian filters — halo-free local contrast
**Paris, Hasinoff, Kautz — SIGGRAPH 2011**, with the **fast approximation by Aubry, Paris, Hasinoff, Kautz, Durand** (~50× faster, 350 ms/MP on a single CPU core → trivially real-time on GPU). Pure image-pyramid math. This is what "clarity" and "texture" sliders should actually be.
[original](https://people.csail.mit.edu/sparis/publi/2011/siggraph/Paris_11_Local_Laplacian_Filters.pdf) · [fast version](https://imagine.enpc.fr/~aubrym/projects/llf/texts/2014-fast-laplacian-filter.pdf)

### Deep bilateral learning (HDRnet)
**Gharbi, Chen, Barron, Hasinoff, Durand — MIT CSAIL + Google, SIGGRAPH 2017.** A CNN predicts local affine transforms in a bilateral grid from a *low-res* input; full-res application is a cheap slice. Runs real-time on mobile GPUs. Note it's the same bilateral-grid trick as BGU, learned rather than fitted.
[paper](https://groups.csail.mit.edu/graphics/hdrnet/data/hdrnet.pdf) · [code](https://github.com/google/hdrnet)

### Joint demosaicking + denoising
**Gharbi, Chaurasia, Paris, Durand — MIT CSAIL, SIGGRAPH Asia 2016.** The canonical CNN treating demosaic and denoise as one problem rather than two sequential ones. Quality ceiling reference for the eventual ML path.

### Halide — the performance-engineering idea
**Ragan-Kelley, Adams, Paris, Levoy, Amarasinghe, Durand — MIT CSAIL, PLDI 2013.** Separates *what* an image pipeline computes from *how* it's scheduled (tiling, vectorization, parallelism, recompute-vs-store). The **auto-scheduler** (Mullapudi, Adams, Sharlet, Ragan-Kelley, Fatahalian, SIGGRAPH 2016) generates schedules in seconds that match or beat expert hand-tuning. MIT-licensed C++, used in Photoshop and Google's camera pipeline.
[Halide](https://halide-lang.org/) · [PLDI paper](https://people.csail.mit.edu/jrk/halide-pldi13.pdf) · [auto-scheduler](http://graphics.cs.cmu.edu/projects/halidesched/mullapudi16_halidesched.pdf)

*Relevance:* not a replacement for the Metal compute DAG, but a strong candidate for generating **CPU fallback paths** from the same algorithm definition — avoiding darktable's write-everything-twice tax. Also how `google/bgu` is implemented.

### MIT-Adobe FiveK — free ground truth
**Bychkovsky, Paris, Chan, Durand — MIT CSAIL + Adobe, CVPR 2011.** 5,000 RAW photos, each retouched by **five trained photographers**, giving five distinct global adjustment styles per image.
[dataset](https://groups.csail.mit.edu/graphics/fivek_dataset/) · [paper](https://people.csail.mit.edu/vladb/photoadjust/db_imageadjust.pdf)

Three concrete uses: (1) validate that auto-enhance moves images toward professional retouching rather than away; (2) derive built-in "looks" from real photographers' choices instead of inventing them; (3) training data if a learned auto-enhance ever ships. Also see **PPR10K** for portrait retouching specifically.

### Stanford lineage — burst photography
Marc Levoy's **Frankencamera** (Stanford Graphics Lab) established programmable computational photography and led directly to Google's HDR+. Relevant less as an algorithm than as the origin of the exposure-fusion tone mapping already in section 2.
[Frankencamera](https://graphics.stanford.edu/papers/fcam/) · [CACM paper](https://graphics.stanford.edu/papers/fcam/adams-frankencamera-cacm12.pdf)

### Recent work worth tracking (2023–2026)
- **Image-adaptive 3D LUTs with bilateral grids** — ECCV 2024. Combines the two ideas above for spatially-aware real-time enhancement. [ACM](https://dl.acm.org/doi/10.1007/978-3-031-72967-6_6) · [PyTorch code](https://github.com/WontaeaeKim/LUTwithBGrid)
- **NILUT** — conditional neural implicit 3D LUTs. [arXiv](https://arxiv.org/pdf/2306.11920)
- **Pixel-adaptive MLPs for real-time enhancement** — 2025. [arXiv](https://arxiv.org/pdf/2507.12135)
- **LoR-LUT** — compact 3D LUTs via low-rank residuals. [arXiv](https://arxiv.org/pdf/2602.22607)

### → Orion picks
- **Build the bilateral grid + BGU as first-class DAG node types in M1**, before they're needed. They convert the entire "this algorithm is too slow to be interactive" problem class into a solved one, and `google/bgu` gives a reference implementation.
- Local Laplacian (fast version) is the clarity/texture slider. Not a nice-to-have — it's the difference between "contrast slider" and "looks professionally retouched."
- Evaluate Halide for CPU fallback code generation rather than hand-writing a second implementation of every node.
- Pull FiveK early: it's the only free way to check auto-enhance against what five real photographers actually did.
