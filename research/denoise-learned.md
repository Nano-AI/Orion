# Learned denoising — Core ML, and whether it fits

Written 2026-08-01 for M5's line **"ML denoise (NAFNet-class via Core ML) as a
background pass, not a live slider"**, which has been on the roadmap since the
first milestone list and had never been investigated.

**This is research and a decomposition. Nothing was built, nothing was
benchmarked, and no gate was run.** Every number below is either read out of
this repository, computed from a published figure, or **marked as a guess**.
`research/highlight-reconstruction.md`'s estimate was wrong by 16× because
nobody measured before costing; the marks below exist so the same mistake is at
least visible.

`research/detail.md` §"Noise reduction" holds the entry for the denoiser that
ships today. This file is about what sits beyond it.

---

## 0. The verdict, first

**A learned denoiser is buildable and worth building, but it cannot be a node in
the develop graph, and the model that fits is not one whose weights can be
downloaded.**

Three findings drive that, in descending order of how much they cost:

1. **The domain is wrong, and not in the direction the roadmap line assumed.**
   Orion's denoise runs **after** the demosaic and **before** the color matrix,
   in linear camera RGB. Published denoisers come in two flavours: sRGB-domain
   (trained on gamma-encoded, white-balanced, tone-mapped 8-bit) and raw-domain
   (trained on the Bayer mosaic). Orion's insertion point is **neither**. §4.
2. **Nothing ships as a graph node.** One fp16 32-channel activation at 24 Mpx
   costs **1,480 MiB — exactly what the entire existing denoise chain costs**,
   and a U-net needs dozens of them. It has to be tiled, and a tiled network
   with a content-independent pass count is a different scheduler from the one
   this engine has. §3.4, §6.
3. **The weights are the problem, not the architecture.** NAFNet's *code* is
   MIT and its *paper* is ECCV 2022; the raw-domain training data that would
   make it correct for Orion is the part with the licence. §5.

The recommendation is in §7: **five sessions, of which the first two are
measurements and the third is a decision point where "stop here" is a real
outcome.** Do not start at session 3.

---

## 1. What Orion's denoise is now — and a correction to the premise

The brief for this file said Orion's noise handling is pre-demosaic. **Half of
that is right, and the half that is wrong is the half that decides everything
below.**

Read out of `engine/src/pipe/DevelopPipeline.cpp`, the node order is:

```
linearize → rcd:dirs → rcd:lowpass → rcd:green → rcd:red/blue
  → highlights → hl:mask → hl:pull ×11 → hl:push ×11 → hl:fill
  → denoise:blur 0..3 → denoise:shrink 3..0
  → lens → spots:measure → spots:apply → sharpen
  → camera->working → ...
```

| Piece | Where it runs | Domain it sees |
|---|---|---|
| **The noise model fit** — `estimateNoise(const BayerImage&)`, `engine/src/raw/NoiseProfile.cpp` | CPU, on the **mosaic**, before any node | Bayer, sensor counts, black-subtracted |
| **The denoise filter** — `denoise:blur 0..3`, `denoise:shrink 3..0` | GPU, **after RCD**, before `camera->working` | Linear **camera RGB**, `rgba16f`, three channels, demosaiced |

The node's own comment says why: `var = a·x + b` only holds in linear camera
RGB, and the color matrix would mix the channels' variances along with the
channels. So the *parameters* are estimated where the noise is independent per
photosite, and the *filter* is applied where the pixels are.

**That split is the architectural fact this whole file turns on.** Orion has a
third domain that almost no published denoiser is trained for, and it is a
domain the codebase chose deliberately and for a good reason.

### The method, and its citations

All of this is already in `research/detail.md` and is not restated here beyond
the shape: à-trous / starlet decomposition (Starck, Fadili & Murtagh, IEEE TIP
16(2), 2007), non-negative garrote shrinkage (Gao, 1998), MAD → σ with 1.4826
(Donoho & Johnstone, Biometrika 81(3), 1994), and the Poisson–Gaussian model
fitted per frame (Foi, Trimeche, Katkovnik & Egiazarian, IEEE TIP 17(10), 2008).
Four scales. Eight nodes. Two controls — luminance and color.

### What it costs today

| | |
|---|---|
| Nodes | **8** of the graph's 173 |
| Memory | **8 × 185 MiB = 1,480 MiB** of the graph's 7,186 MiB — **20.6%** |
| Latency | ⚠ **Not separately measured.** No per-node timing for the denoise chain exists in `STATUS.md`, `ROADMAP.md` or the bench output |

At 6024×4024 = 24,240,576 px, one `RGBA16Float` node is 193,924,608 B = **185
MiB** — the same number `research/highlight-reconstruction.md` measured for its
apply pass, and the same 194 MB decision #103 measured for the creative
vignette. The unit is consistent across the codebase; use it.

---

## 2. The gap, and how much of it can be quantified from here

**It cannot be quantified from here, and saying otherwise would be inventing a
number.** What *can* be stated:

**What is measured.** `research/detail.md` records one measurement, on a Sony
ILCE-7M3 night frame at +2.6 EV over a flat 903×603 sky patch: per-channel
standard deviation falls from 0.1409 / 0.1276 / 0.1521 (off) to 0.0558 / 0.0532
/ 0.0516 (luminance 2.0, color 3.0). That is a **noise** measurement on a
**flat** region. It says the shrinkage removes scatter. It says nothing at all
about what it does to detail, because a flat patch has none.

**What is not measured, and is the actual gap.**

| Question | Status |
|---|---|
| PSNR / SSIM against a clean reference | **No clean reference exists in this repo.** There is no paired noisy/clean fixture, so no full-reference metric can be computed |
| Detail retained at a given noise reduction | Unmeasured. This is the axis on which wavelet shrinkage is known to lose, and the axis nobody here has looked at |
| Chroma blotching at high ISO | Unmeasured |
| Behaviour where the per-frame fit fails | Partly known — quantile bins exist *because* equal-width bins abandoned the fit on night frames. `kMinSamples = 4000` and `kMinBrightnessSpread = 0.02` are the give-up conditions |

**The literature's own number for the gap, which is about a different pipeline
and must be read as an upper bound on the *interesting* range, not as Orion's
gap:** on SIDD, the published ordering runs from classical methods in the
mid-30s dB to NAFNet's 40.30 dB. BM3D — which `detail.md` explicitly rejected on
GPU-hostility grounds, not quality grounds — is the strongest classical entry,
and à-trous garrote shrinkage is materially *below* BM3D. So the honest
statement is: **there is a real gap, its size is unknown for this pipeline, and
measuring it is session 1.**

⚠ **Do not quote a dB figure for Orion's denoiser.** None has been computed.

---

## 3. Can a learned model live in this architecture at all?

### 3.1 Core ML is Objective-C, so the facade is not the wall

The brief assumed Core ML is Swift/ObjC-facing and therefore on the far side of
the POD facade from the C++ graph. **That is not true, and it is checkable
without a network connection.**

Verified against the SDK headers on this machine
(`/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`):

| Symbol | Header | Availability |
|---|---|---|
| `MLModel`, `MLFeatureProvider`, `MLModelConfiguration` | `CoreML.framework/Headers/MLModel.h` etc. | Objective-C classes, `API_AVAILABLE(macos(10.13))` |
| `+[MLFeatureValue featureValueWithPixelBuffer:]` | `MLFeatureValue.h:60` | macOS 10.13 |
| `-[MLMultiArray initWithPixelBuffer:shape:]` | `MLMultiArray.h:178` | macOS 12.0 |
| `MLMultiArrayDataTypeFloat16` | `MLMultiArray.h:19` | macOS 12.0 |

All of it is plain Objective-C. The engine already compiles three `.mm`
translation units (decision #86, `Resources.mm`), so **Core ML can be called
from inside the engine without Swift, without the facade, and without a new
language in the build.**

⚠ **One thing does not change: `no C++ exception may escape the facade`, and
Core ML fails by `NSError` out-parameter, not by exception.** An ObjC++ wrapper
that turns an `NSError` into a C++ throw and lets it travel would terminate the
process. The wrapper must return a status code, exactly as the rest of the
engine's `.mm` files do.

### 3.2 The zero-copy path exists, and it is IOSurface

`MLMultiArray initWithPixelBuffer:shape:` (macOS 12) takes a `CVPixelBuffer`.
A `CVPixelBuffer` created IOSurface-backed can be wrapped as an `MTLTexture`
through `CVMetalTextureCache`, and Metal and Core ML then address the same
pages. So a full GPU→CPU→GPU round trip is **avoidable in principle**.

⚠ **In principle.** Whether Core ML actually honours it without an internal
copy — especially when it schedules onto the Neural Engine, which has its own
memory expectations — is **not verified here and is the single largest unknown
in this file.** It is session 2.

The precedent in the repo is the opposite shape and should not be read as
support for this one. `app/SubjectMatte.swift` runs Vision from **Swift**, on a
**1024 px long edge** picture that the engine renders and reads back
(`renderForAnalysis`), and returns a **single-channel alpha** raster. That is a
~1 Mpx round trip, on demand, for a mask. A denoiser is a 24 Mpx round trip, of
three-channel `f16`, that has to land back in the middle of the graph. The two
are not the same problem and the existing path does not generalise to it.

### 3.3 MPSGraph is the other door, and it is the one that fits Metal

`MetalPerformanceShadersGraph` is also pure Objective-C, and
`MPSGraphTensorData` has, verified in
`MPSGraphTensorData.h:57` and `:72`:

```objc
- (instancetype)initWithMTLBuffer:(id<MTLBuffer>)buffer
                            shape:(MPSShape *)shape
                         dataType:(MPSDataType)dataType;
```

An `MTLBuffer` goes in directly — no pixel buffer, no IOSurface dance, no
readback, and the work lands on the same `MTLCommandQueue` the graph already
owns. `MPSGraphConvolutionOps.h`, `MPSGraphPoolingOps.h`,
`MPSGraphNormalizationOps.h` and `MPSGraphArithmeticOps.h` are all present, so a
NAFNet-shaped network is expressible.

**The trade is explicit:**

| | Core ML | MPSGraph |
|---|---|---|
| Language | ObjC (usable from `.mm`) | ObjC (usable from `.mm`) |
| Input | `CVPixelBuffer` / `MLMultiArray`; zero-copy needs IOSurface and is unverified | `MTLBuffer` directly, verified in the header |
| Neural Engine | Yes, if the model compiles for it | **No** — GPU only |
| Model authoring | A compiled `.mlpackage`; conversion via `coremltools` (BSD-3-Clause, verified via the GitHub licence API) | **The network is built by hand in ObjC, layer by layer** |
| Maintainability | One artefact in the bundle | ⚠ Hundreds of lines of hand-written graph construction — **against the 50–150 line rule and the "no 1000-line anything" constraint** |

**Recommendation: Core ML, and MPSGraph only if session 2 proves the copy is
unavoidable and fatal.** Hand-building a U-net in `MPSGraph` calls is the kind
of file the maintainability constraint exists to forbid.

### 3.4 The graph itself is the thing that cannot hold it

Even with the language question answered, a learned denoiser is not a node:

- **`pipeline_.add` builds a static DAG at construction**, with one texture per
  node, sized to the frame. A tiled network is *N* invocations over a moving
  window, where *N* is a function of the frame size. That is expressible — but
  as one node it would need a shader that is a whole network, and as many nodes
  it is 126 tiles × the layer count.
- **The memory does not fit** at full resolution (§6), and tiling means the
  node's output is assembled over many dispatches rather than written once.
- **The engine's caching model is per-node dirty propagation.** A pass that
  takes seconds must not be re-run by a slider, and the existing mechanism for
  "do not re-run this" is decision #92's `unchangedPrefix` guard — which is
  about *parameters not changing*, not about a pass being too expensive to run
  at all. A background pass needs a different concept: a result that is computed
  once, cached against the frame and the model, and *composited* rather than
  recomputed.

**So: not a graph node. An on-demand pass whose output enters the graph as an
input texture**, in the same structural position `mask:0`'s uploaded raster
occupies — computed elsewhere, uploaded once, read by the graph. That is an
existing, working pattern in this codebase (decision #79, the raster matte), and
it is the only one of the three that already has precedent.

---

## 4. Raw, sRGB, or the third domain Orion actually has

**This is the finding, and it is the one that would have produced a purple-cast
class of error if it had been skipped.**

### The two published domains

**sRGB domain.** The SIDD sRGB benchmark, which is what NAFNet's 40.30 dB and
Restormer's 40.02 dB are measured on, is *processed* output: demosaiced, white
balanced, color-matrixed, tone-mapped, gamma-encoded, 8-bit. A network trained
there has learned the noise distribution **after** a pipeline squashed it —
which is a different, spatially-correlated, signal-dependent-in-a-different-way
distribution from what a sensor produces.

**Raw domain.** Brooks, Mildenhall, Xue, Chen, Sharlet & Barron, *Unprocessing
Images for Learned Raw Denoising* (arXiv:1811.11127, submitted 27 November 2018;
CVPR 2019) is the paper that names this problem: denoisers "are applied to real
raw camera sensor readings but ... are often trained on synthetic image data",
and the pipeline's "gain, color correction, tone mapping, etc." have been "often
overlooked". Their fix is to *unprocess* internet photographs back to synthetic
raw, train there, and evaluate the loss through a model of the forward pipeline.
It buys **14–38% lower error and 9–18× faster** than the prior state of the art
on the Darmstadt Noise Dataset.

That paper is the citation for "the domain matters", and it is dated,
published, and heavily built on. It should be the first entry in any bibliography
for this work.

### Orion's domain is neither

The insertion point (§1) is **after RCD, before the color matrix**: linear
camera RGB, three channels, `rgba16f`, unbounded above, white-balanced by
`linearize`, and clipped to one common white level by decision #29.

| Against a raw-domain model | Against an sRGB-domain model |
|---|---|
| Orion's data is **demosaiced**. Raw models take a 4-channel packed Bayer plane and learn the CFA phase. Feeding them RGB is feeding them a different tensor | Orion's data is **linear and unbounded**. sRGB models expect ~2.2-gamma-encoded values in [0,1] |
| The mosaic is **upstream of RCD and upstream of #29's clip** — moving the denoiser there means reopening both | Orion's data is in **camera primaries**, not sRGB/Rec.709. Chroma noise looks different in a space whose primaries are not the training space's |
| Would need the whole denoise/demosaic order inverted, which is the correct order for a raw model and the wrong order for everything downstream of it | The noise at that point has **not** been through tone mapping, which is exactly the transformation the sRGB training distribution encodes |

**Three ways out, and their costs:**

| Option | What it means | Cost |
|---|---|---|
| **A — move the model upstream, to the mosaic** | Replace the wavelet chain with a raw-domain network operating on the packed Bayer plane, before RCD. Matches the literature exactly | Reopens decision #29 (the pre-demosaic clip) and the demosaic order. The **model must then also be responsible for what RCD does to noise**, which is the whole reason denoise-before-demosaic is the published order |
| **B — move the model downstream, past the display transform** | Run an sRGB-domain checkpoint on the display-referred image, as `SubjectMatte` already does for Vision | ⚠ **Violates decision #6** — scene-referred, exactly one display transform. Denoising after AgX means the denoise is baked into a specific look and cannot be re-graded. Also cannot fix chroma blotches that the matrix already amplified |
| **C — train for Orion's actual domain** | Take Brooks et al.'s unprocessing idea and stop it one stage earlier: unprocess to *linear camera RGB after demosaic* rather than to raw | The honest answer, and the expensive one. §5 |

**Option A is the one the literature supports and the one this file recommends
investigating** — but it is a much larger change than "add a node", because it
inverts the denoise/demosaic order and reopens a settled decision. Option B is
cheap and wrong. Option C is §5's "train our own", with §5's price.

⚠ **A model trained on SIDD-sRGB applied at Orion's insertion point is the same
class of error as the purple cast.** It would produce a plausible-looking
result, it would be wrong for a reason invisible to inspection, and no test in
either suite would catch it. If nothing else in this file is carried forward,
carry that.

---

## 5. The models, and — the load-bearing part — the licences

Every row below was checked, not remembered. Repository licences were read
through the GitHub licence API (`gh api repos/<owner>/<repo>/license`); papers
were opened at the URL given.

### The architectures

| Model | Citation | Opened | Verdict for Orion |
|---|---|---|---|
| **DnCNN** | Zhang, Zuo, Chen, Meng & Zhang, *Beyond a Gaussian Denoiser: Residual Learning of Deep CNN for Image Denoising*, IEEE TIP 26(7), 2017. [arXiv:1608.03981](https://arxiv.org/abs/1608.03981), submitted 13 August 2016 | ✅ abstract | **Historically important, superseded.** Trained for additive white Gaussian noise at a fixed σ — the one noise model a sensor definitively does not have. Orion already fits Poisson–Gaussian per frame, which is strictly better information than DnCNN can use |
| **FFDNet** | Zhang, Zuo & Zhang, *FFDNet: Toward a Fast and Flexible Solution for CNN based Image Denoising*, IEEE TIP 27(9), 2018. [arXiv:1710.04026](https://arxiv.org/abs/1710.04026), submitted 11 October 2017 | ✅ abstract | **The most interesting of the pre-2020 four for Orion**, and not for its quality. It "accepts a tunable noise level map as the input", handles σ 0–75 with one network, and removes **spatially variant** noise. A per-pixel noise-level map is precisely what `estimateNoise`'s `var = a·x + b` already produces, so the two compose: Orion's fitted model *is* the conditioning input. ⚠ Still AWGN-derived, and still sRGB |
| **Noise2Noise** | Lehtinen, Munkberg, Hasselgren, Laine, Karras, Aittala & Aila, *Noise2Noise: Learning Image Restoration without Clean Data*, [arXiv:1803.04189](https://arxiv.org/abs/1803.04189), submitted 12 March 2018, ICML 2018 | ✅ abstract | **A training technique, not a model.** Relevant only if Orion trains its own — and then it is very relevant, because it removes the need for clean ground truth |
| **Noise2Void** | Krull, Buchholz & Jug, *Noise2Void — Learning Denoising from Single Noisy Images*, CVPR 2019. [arXiv:1811.10980](https://arxiv.org/abs/1811.10980), submitted 27 November 2018 | ✅ abstract | Same category as Noise2Noise and weaker by the authors' own account — it "does not require noisy image pairs, nor clean target images" and in exchange "compares favorably to **training-free** denoising methods", which is a lower bar than Noise2Noise's. Its motivating case is biomedical imaging where pairs are infeasible. **Orion can synthesise pairs** (§4 option C), so it is giving up information it has |
| **Restormer** | Zamir, Arora, Khan, Hayat, Khan & Yang, *Restormer: Efficient Transformer for High-Resolution Image Restoration*, CVPR 2022 | ⚠ abstract not opened; **licence verified** | 40.02 dB on SIDD at **140 GMACs**. Strictly dominated by NAFNet on both axes |
| **NAFNet** | Chen, Chu, Zhang & Sun, *Simple Baselines for Image Restoration*, ECCV 2022. [arXiv:2204.04676](https://arxiv.org/abs/2204.04676), submitted 10 April 2022 | ✅ abstract + Table 6 | **40.30 dB on SIDD at 65 GMACs**, MACs "estimated by an input with the spatial size of 256×256". Best quality-per-compute of the six |

Published comparison, from NAFNet's Table 6, at 256×256:

| Method | SIDD PSNR | SSIM | MACs (G) |
|---|---|---|---|
| MPRNet | 39.71 | 0.958 | 588 |
| Restormer | 40.02 | 0.960 | 140 |
| **NAFNet** | **40.30** | **0.962** | **65** |

⚠ **All three are sRGB-domain numbers.** They are the right way to rank
architectures and the wrong way to predict what any of them does at Orion's
insertion point (§4).

### The licences — and this is where it stops being an architecture question

| Artefact | Licence | How checked | Usable? |
|---|---|---|---|
| **NAFNet code** | **MIT** (+ Apache-2.0 for the vendored BasicSR) | `LICENSE` in `megvii-research/NAFNet`, read directly | ✅ Yes |
| **Restormer code** | **MIT** | GitHub licence API, `swz30/Restormer`, `LICENSE.md` | ✅ Yes |
| **DnCNN code** | ⚠ **No licence file at all.** `gh api repos/cszn/DnCNN/license` returns 404; the repository root has no `LICENSE` | GitHub licence API + root listing | ❌ **No.** No licence means no grant. All rights reserved |
| **FFDNet code** | ⚠ **No licence file.** `gh api repos/cszn/FFDNet/license` returns 404 | GitHub licence API | ❌ **No** |
| **KAIR** (the same author's later toolbox, which re-implements DnCNN and FFDNet) | **MIT** | GitHub licence API, `cszn/KAIR` | ✅ Yes — this is the route to those two architectures if they are ever wanted |
| **`coremltools`** | **BSD-3-Clause** | GitHub licence API, `apple/coremltools` | ✅ Yes |
| **SIDD** (the dataset NAFNet's denoising weights are trained on) | The project page states "The dataset and the associated code repositories are under the MIT License" | Project page at `abdokamel.github.io/sidd` | ⚠ **Probably yes — verify before shipping.** See the caveat below |
| **DND / Darmstadt Noise Dataset** | "made freely available to academic and non-academic entities for **non-commercial purposes** such as academic research, teaching, scientific publications, or personal experimentation" | Project page, `noise.visinf.tu-darmstadt.de` | ❌ **No for a shipping product.** Benchmark-only |

**Citation:** SIDD is Abdelhamed, Lin & Brown, *A High-Quality Denoising Dataset
for Smartphone Cameras*, CVPR 2018. DND is Plötz & Roth, *Benchmarking Denoising
Algorithms with Real Photographs*, CVPR 2017.

⚠ **A caveat that must not be lost.** The SIDD MIT claim was read off the
project page by an automated fetch and is a single-source claim about the one
thing that decides whether weights can ship. **Read the licence file in the
distributed archive before relying on it.** Decision #78 already established the
principle that matters here: *weights inherit their training data's ambiguity,
however permissive the architecture's code is.* NAFNet being MIT does not make
NAFNet's checkpoints MIT.

⚠ **And a second one, larger.** SIDD is **smartphone** sRGB. Even with a clean
licence, NAFNet's SIDD checkpoint is trained on small-sensor phone noise, in the
sRGB domain, and Orion's target is a full-frame ILCE-7M3 in linear camera RGB.
The licence being clean does not make the weights correct.

### "Train our own" — the cost, stated

Option C from §4 is the only one that is *correct* rather than *available*. What
it actually costs:

| Piece | Cost | Confidence |
|---|---|---|
| A Python training environment, PyTorch, a GPU | Not a Mac. Cloud rental or a borrowed machine | Certain |
| A dataset of camera-RGB noisy/clean pairs | **Does not exist.** It has to be synthesised — Brooks et al.'s unprocessing, stopped one stage earlier (§4, option C), driven by Orion's own `estimateNoise` fit | ⚠ Guess: this is 1–2 sessions of Python that is not engine work |
| Training | ⚠ **Guess: days of GPU time, and several rounds of it.** Nobody in this repo has done it | Guess |
| `coremltools` conversion, `.mlpackage`, bundling | BSD-3, well-trodden | Fairly confident |
| **A Python step in a solo C++/Swift maintainer's build** | Decision #78 already refused a feature partly on these grounds | Certain |
| The maintenance tail | Every new camera is a question about whether the model generalises | Certain |

**This is a multi-week epic, and `STATUS.md` already says so** — "M5 is months,
not sessions ... Core ML denoise ... each a multi-week epic on its own". That
line is correct and this file is the evidence for it.

---

## 6. Memory and latency, in this graph's units

### Memory — the arithmetic that settles §3.4

The frame is 6024 × 4024 = 24,240,576 px.

| Quantity | Bytes/px | At 24.24 Mpx |
|---|---|---|
| One `RGBA16Float` node | 8 | **185 MiB** |
| One **fp16, 32-channel** activation | 64 | **1,480 MiB** |
| The whole existing denoise chain (8 nodes) | 64 | **1,480 MiB** |

**Those last two are exactly equal, and not by coincidence** — 32 channels × 2
bytes is 64 B/px, and so is 8 nodes × 8 B/px. **One feature map of a
width-32 network at full resolution costs the entire denoiser Orion ships.**

A NAFNet-shaped U-net at width 32 with four levels, one live activation per
level, halving in cost each level down (area ÷4, channels ×2):

| Level | Resolution | Channels | Cost |
|---|---|---|---|
| 0 | full | 32 | 1,480 MiB |
| 1 | 1/2 | 64 | 740 MiB |
| 2 | 1/4 | 128 | 370 MiB |
| 3 | 1/8 | 256 | 185 MiB |
| bottleneck | 1/16 | 512 | 93 MiB |
| **one per level** | | | **2,868 MiB** |

⚠ **And that is the floor, not the figure.** Every block needs two to three live
tensors, and the encoder's outputs stay alive across the whole U for the skip
connections. **Guess: 4–8 GiB in practice** — on top of the graph's existing
7,186 MiB, on a machine that may have 8 GB total. `ROADMAP.md` already flags
"6971 MiB of intermediates ... what happens on 8 GB".

**So it tiles.** At 512×512 with a 32 px halo on each side (stride 448):

| | |
|---|---|
| Tiles | ⌈(6024−512)/448⌉+1 = 14 across, ⌈(4024−512)/448⌉+1 = 9 down → **126 tiles** |
| Pixels processed | 126 × 262,144 = 33.0 Mpx — **1.36× the frame**, the halo tax |
| Peak activation, one tile, one per level | 16 + 8 + 4 + 2 + 1 = **31 MiB** |
| Peak in practice | ⚠ **Guess: ~100 MiB.** Three live tensors per block |

**100 MiB is affordable. 4 GiB is not. Tiling is not an optimisation here, it is
the only version that runs.**

⚠ **The halo width is a guess.** The correct halo is the network's receptive
field, which for a four-level U-net with 3×3 convolutions is substantially more
than 32 px. Getting it wrong produces **visible tile seams**, which is the
classic failure of this approach and is exactly the kind of thing a five-line
assertion catches and inspection does not. Session 3 measures it.

### Latency — computed, not measured

From NAFNet's own Table 6: 65 GMAC at 256×256 = 65×10⁹ / 65,536 = **0.992
MMAC/px**.

| | MACs | FLOPs (2×) |
|---|---|---|
| Frame-exact, 24.24 Mpx | 24.0 TMAC | **48.1 TFLOP** |
| Tiled with the halo tax, 33.0 Mpx | 32.8 TMAC | **65.5 TFLOP** |

⚠ **No sustained fp16 convolution throughput has been measured on this machine,
so this cannot be turned into milliseconds here.** As a function:

| Sustained fp16 throughput | Time for one 24 MP frame, tiled |
|---|---|
| 5 TFLOP/s | 13.1 s |
| 10 TFLOP/s | 6.6 s |
| 20 TFLOP/s | 3.3 s |
| 40 TFLOP/s | 1.6 s |

The only published anchor in this repository is `detail.md`'s: DxO's DeepPRIME
at roughly 2 MP/s, about **12 s for a 24 MP frame**. ⚠ **That citation could not
be re-verified for this file** — DxO's support page returns HTTP 403 to an
unauthenticated fetch. It is carried forward as an existing repo claim, flagged,
and it should be re-checked by hand.

**Whatever the constant, the conclusion is the same and does not depend on it.**
The M0 target is a **sub-16 ms drag**. The cheapest row above is 1.6 s — **100×
the entire frame budget**, and that is the optimistic hardware. There is no
arrangement of these numbers in which this is a live slider.

**Named, per the brief: this is an on-demand / export-time pass. Not a graph
node. Not a slider.**

---

## 7. The decomposition

Sessions, in order, in the style of `research/highlight-reconstruction.md`'s
piece table. **Node and memory costs are for what lands in the graph** —
sessions 1–3 land nothing in it, which is the point.

| # | Session | Cost | Guess? |
|---|---|---|---|
| 1 | **Measure the gap.** Build a paired fixture: a clean synthetic frame, `estimateNoise`'s own Poisson–Gaussian model applied forward to make a noisy twin, both through the real `DevelopPipeline`. Report PSNR/SSIM and a detail metric with the denoiser off, at luminance 2.0, and at 4.0. ⚠ **This is the session that decides whether the rest is worth doing** — if the shipped denoiser is 2 dB off the ceiling, stop | **+0 nodes, +0 MiB.** A test fixture and a bench probe. ~1 session | Confident. This is ordinary test work |
| 2 | **Measure the round trip, with no model in it.** An ObjC++ file in the engine that takes an `MTLTexture`, wraps an IOSurface-backed `CVPixelBuffer`, hands it to a **trivial identity `.mlpackage`**, and gets it back. Time it at 24 Mpx. Assert the pixels are bit-identical. ⚠ **This is where "zero-copy" is proved or disproved**, and it is the largest unknown in this file. If it is a real copy, MPSGraph becomes the recommendation and the estimate changes | **+0 nodes, +0 MiB.** ~1 session | Confident about the shape; the *answer* is the unknown |
| 3 | ⚠ **DECISION POINT — the domain, and it is a written argument, not code.** Choose §4's A, B or C, log it as a decision, and be willing for the answer to be "none, stop here". Everything after this session is expensive and none of it is recoverable if the domain is wrong | **0 files.** ~half a session | Certain that it is needed; the outcome is open |
| 4 | **The tiler, with no network in it.** Split → identity → reassemble, at 512 with a parameterised halo. ⚠ **Assert the reassembly is bit-identical to the input** — a seam is invisible to inspection and obvious to a five-line assertion, which is this codebase's own stated lesson twice over. Measure the receptive field the chosen model actually needs and set the halo from it, do not guess it | **+0 nodes** in the develop graph — this is an off-graph pass. **~100 MiB** working set. ~1 session | ⚠ The 100 MiB is a guess (§6). The halo width is a guess until measured |
| 5 | **The model, whichever session 3 chose.** Convert or train, bundle the `.mlpackage`, run it through session 4's tiler, and composite the result into the graph as an **uploaded texture** in the position `mask:0`'s raster occupies (decision #79's pattern) | **+1 node, +185 MiB** for the uploaded result, plus the tiler's working set. ⚠ Session count **unknown by construction**: option B is ~1 session, option A is 2–3, **option C is weeks** | ⚠ Guess. This is the row `highlight-reconstruction.md` would tell you not to trust |
| 6 | **The control, and the cache invalidation.** One button, not a slider: the pass runs on demand, the result is cached against the frame and the model version, and *no* parameter change re-runs it. ⚠ The existing `unchangedPrefix` guard (decision #102) is about parameters not changing, not about a pass being too expensive to run — this needs its own concept | ~1 session, ~10 files | Fairly confident — it is the shape decision #79 already built once |

**Total, honestly: 4.5 sessions to a decision point that may say stop, plus an
unbounded tail.** Sessions 1–4 are worth doing on their own merits regardless of
whether a model ever ships: session 1 gives Orion the paired fixture and the
full-reference metric it does not have, and session 4 gives it a tiler, which is
also what a full-resolution export path wants.

⚠ **Do not start at session 5.** Every previous estimate in this repository that
was wrong was wrong because the measurement came after the cost.

---

## 8. What must not be done along the way

- **Shipping an sRGB-domain checkpoint at the current insertion point.** §4. It
  would look plausible and be wrong for an invisible reason, which is the
  definition of the failure `research/README.md` exists to prevent.
- **Bundling weights whose training data's licence has not been read.** Decision
  #78 settled this for segmentation and the argument transfers unchanged:
  weights inherit their data's terms. DND is explicitly non-commercial; SIDD's
  MIT claim is single-source and unverified in the archive.
- **Hand-building the network in `MPSGraph` calls** unless session 2 forces it.
  It is a thousand-line file by construction, and the maintainability constraint
  is a hard one.
- **Letting an `NSError` become a C++ exception that crosses the facade.** §3.1.
- **Making it a slider.** §6 — the cheapest arithmetic is 100× the frame budget.
- **Adding it as a graph node.** §3.4, §6.
- **Quoting a dB number for Orion's current denoiser.** §2. None exists.

---

## 9. Honest limits of this file

- **Nothing was built and nothing was benchmarked.** No `orion-tests` run, no
  `orion-viewport-tests` run, no build. This file was written from the source,
  the SDK headers, the papers and the licence APIs.
- **One paper's abstract was not opened: Restormer's.** Its *licence* was
  verified through the GitHub API and its numbers are quoted from **NAFNet's**
  Table 6 rather than from its own paper — which is the honest attribution and
  is why the row above says so. Nothing here rests on it: it is dominated by
  NAFNet on both axes in the table that lists them together.
- **The five others were opened at the arXiv URLs given** and their titles,
  author lists and dates are taken from those pages, not from memory. This
  matters because a citation in this repository was found wrong in five files
  the same week — an author named Tang who was Tappen — and nothing in either
  test suite can catch that class.
- **The SIDD licence is a single automated read of a project page**, and it is
  the claim that decides whether weights can ship.
- **The DxO throughput figure could not be re-verified** (HTTP 403).
- **Every memory figure past the 185 MiB unit is arithmetic, not measurement.**
  The 4–8 GiB, the ~100 MiB tile working set and the 32 px halo are marked as
  guesses where they appear.
- **Whether Core ML's `CVPixelBuffer` path is genuinely zero-copy is unverified**
  and is the largest single unknown. It is session 2 for that reason.

---

## History

- **2026-08-01** — Written, for M5's never-investigated "Core ML denoise" line.
  Research and decomposition only; nothing built, by design. Corrected the
  premise that Orion's noise handling is pre-demosaic — the *fit* is, the
  *filter* is not, and the filter's domain (linear camera RGB, post-demosaic) is
  a third domain no published checkpoint is trained for. Found that Core ML is
  Objective-C and so callable from the engine without crossing the facade, which
  removes the assumed architectural blocker and replaces it with a memory one.
  Verified licences: NAFNet MIT, Restormer MIT, **DnCNN and FFDNet have no
  licence file at all**, DND non-commercial, SIDD claimed MIT and unverified in
  the archive. Recommendation: on-demand pass, not a node, not a slider; five
  sessions with a stop-here decision point at session 3.
