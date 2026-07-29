# Auto-enhance

M3's last item. Measure the picture, set the ordinary sliders.

---

## What is sourced, and what is not

| Piece | Status |
|---|---|
| The percentile statistic and how thresholds are defined | **Sourced** — Simplest Color Balance §3.1 |
| Working on luminance rather than per channel | **Sourced** — same paper, §4.1, and it is the paper's own sentence |
| A cap on how hard auto-contrast may push | **Sourced** — Lisani, Petro & Sbert, IPOL 2012 |
| The mid-grey anchor, 0.461 of maximum | **Sourced** — CIPA DC-004:2004 §2.3(3) |
| **The percentage to clip** | ⚠️ **Inference, not a recommendation.** See below |
| **That an image's median should sit at mid grey** | ⚠️ **An extrapolation the standard does not make.** See below |
| How much clarity and shadow lift to apply | ⚠️ **Taste.** `UNSOURCED.md` |

---

## The percentile stretch

**Limare, Lisani, Morel, Petro & Sbert — *Simplest Color Balance*, Image
Processing On Line, 1 (2011), pp. 297–315.**
[DOI 10.5201/ipol.2011.llmps-scb](https://doi.org/10.5201/ipol.2011.llmps-scb) ·
[PDF](https://www.ipol.im/pub/art/2011/llmps-scb/article.pdf) · published
2011‑10‑24.

The thresholds are quantiles of the value distribution, §3.1:

> *"Vmin and Vmax, the saturation extrema, can be seen as quantiles of the pixel
> values distribution, e.g. first and 99th centiles for a 2% saturation."*
>
> *"With a saturation level s = s1 + s2 in [0, 100[, we want to saturate
> N × s/100 pixels, so Vmin and Vmax are taken from the sorted array at
> positions N × s1/100 and N × (1 − s2/100) − 1."*

`s` is the **total** across both ends. Orion needs only this half of the paper —
the affine stretch it then applies is what the exposure, blacks and whites
sliders already are.

### ⚠️ The paper recommends no percentage. This is the finding that mattered.

There is no sentence in it recommending a value. What exists is §1, *"the
percentage of saturated pixels must be as small as possible"*, and figure
captions: *"Here, the 0% saturation already gives a good result, and 1% is
optimal"*, *"Even a good quality image can benefit from a moderate 1% color
balance"*, with §7 fixing the split half and half. §7 is blunt about it:

> *"It is quite apparent that some saturation is almost always necessary, but
> that the needed percentage is variable."*

The reference C implementation was checked too: its saturation levels are
**mandatory positional arguments with no fallback**. So the widely repeated
"0.5% per side" is a reading of the s = 1% examples, not a citation. Orion uses
it, and records it as inference in `UNSOURCED.md` rather than dressing it as a
recommendation.

### Luminance, not per channel — and the paper hands over the reason

§4.1, on applying the balance independently to R, G and B:

> *"The color of the pixels is modified in the process because each RGB channel
> is transformed by an affine function with different parameters… This can be
> desirable to correct the color of a light source or filter, but **in some
> applications we may want to maintain the colors of the input image**."*

And §1, which settles it for a raw editor that already has a real white balance:

> *"The proposed algorithm therefore provides both a white balance and a contrast
> enhancement. However, note that **this algorithm is not a real physical white
> balance**: It won't correct the color distortions of the capture device or
> restore the colors of the real-world scene."*

Orion's white balance comes from the camera's own reading or the photographer.
An auto-contrast that silently re-balanced colour on top of it would be
overriding a deliberate decision, invisibly, with something the paper itself
says is not a white balance.

### Stated failure cases, worth knowing before trusting the number

- §1: *"this saturation can create flat white regions or flat black regions that
  may look unnatural."*
- §3.5: degenerate when `Vmin = Vmax` — a constant image, or one with fewer than
  the clipped count outside the median. Every pixel is set to that value.
- Fig. 2: **a thin white rim occupying more than 2% of the frame drags the
  threshold to 3%** — a border artefact hijacks the statistic. Relevant to any
  editor where a scanned or bordered frame can appear.
- Fig. 6, a sunset: *"By pushing too far the saturation (3%), the orange pixels
  diminish and a completely unnatural blue color is created."*
- Figs. 13–14: *"The back-lighting problem has no simple solution."*

### A published cap on how far to push

**Lisani, Petro & Sbert — *Color and Contrast Enhancement by Controlled
Piecewise Affine Histogram Equalization*, IPOL 2 (2012), pp. 243–265**,
[DOI 10.5201/ipol.2012.lps-pae](https://doi.org/10.5201/ipol.2012.lps-pae) — the
follow-up Simplest Color Balance §4.1 promises. Its control is a bound on the
slope of the transform rather than a percentile: `smax = 2` *"provides a good
compromise between contrast enhancement and saturation"*, with `smax = 3` used
elsewhere *"to prevent the excessive saturation of the colors"*.

That is a published ceiling on how much contrast an automatic stretch may add,
and Orion uses it as one.

---

## The exposure anchor

**CIPA DC-004:2004**, *Sensitivity of digital cameras* — the industry standard
folded into the ISO 12232:2006 revision, which DC-004 §918 says it anticipates.
Part 1 §2.3(3):

> *"The standard level of digital output to obtain Hm is
> **MAX × 0.461**
> where MAX is the maximum digital output level.
> Fractions are dropped. (118 for 8-bit type)"*

Measured on luminance, not per channel. And 0.461 is exactly where 18% grey
lands through the sRGB transfer function — `sRGB(0.18) = 0.46136`.

**18% is not arbitrary either**, which was worth checking. Explanation §3.2:

> *"the logarithmic center (logarithmic average) is measured at 18% when maximum
> and minimum values of the diffuse reflectance distribution of the subject are
> about 98% and 3.3% respectively."*

`√(0.98 × 0.033) = 0.1798`. It is the geometric mean of an assumed subject
reflectance range.

⚠️ **A wart in the standard's own text.** It says *"fractions are dropped"*, but
`0.461 × 255 = 117.56`, which truncates to **117**, not the 118 it prints.
118 needs `MAX = 256`, while §2.3(3) defines MAX as the maximum *level*, 255.
Industry uses 118. Recorded so nobody re-derives it and concludes this code is
wrong.

### ⚠️ Two extrapolations, both stated

**First: the standard is about a grey chart, not a photograph.** 0.461 is where a
uniform 18% reflectance card should land under controlled lighting. Targeting a
*pictorial image's median* at it is a step the standard does not take. There is
**no published value for the mean or median luminance of a well-exposed
photograph** — that was searched for and not found.

**Second: the standard says so itself.** Explanation §3.2:

> *"The level of sensitivity in itself is voluntary… **there is no single and
> absolute point of definition as long as the tone is in the middle range;
> therefore, it becomes necessary to select some value.**"*

So the anchor is a convention, chosen by a standards body, and Orion adopting it
for a different quantity is a judgement. Better than a number invented here, and
not the same thing as a citation that supports the claim.

### One trap avoided

Mertens et al.'s well-exposedness weight — a Gaussian centred at 0.5 with
σ = 0.2 — is **a per-pixel blending kernel for a bracketed stack, not a claim
about the mean of a photograph.** Orion already uses it correctly, inside
exposure fusion (`research/exposure-fusion.md`). Citing it as evidence that a
well-exposed image averages 0.5 would be exactly the kind of wrong-but-cited
constant this folder exists to prevent.

---

## What auto-enhance actually sets

Auto-enhance is **not a filter and not a node**. It measures the rendered
histogram and writes the user's ordinary sliders, so every decision it makes is
visible, adjustable, undoable and stored in the sidecar. An "auto" that applied
a hidden transform would be a second editing model underneath the first.

| Slider | Driven by |
|---|---|
| Exposure | the median, toward the 0.461 anchor |
| Blacks / whites | the 0.5%-per-side percentiles, capped by the published slope bound |
| Shadow lift (fusion) | how far the shadow percentile sits below the range it could use |
| Clarity | a fixed modest amount — **taste**, `UNSOURCED.md` |
| Dehaze | **left alone.** Deciding whether a frame is hazy needs the dark-channel statistic, which the auto path does not compute — and applying dehaze to a frame with no haze in it is exactly the case the dark channel prior is documented to get wrong |

**It solves rather than derives.** The relationship between the exposure slider
and the rendered histogram runs through AgX, the tone curve and every other
control the user has already set, so it is not invertible in closed form.
Auto-enhance renders, measures, adjusts and repeats. That makes it correct
whatever the curve is doing, and it makes the test an outcome — *did the
percentiles land where they were aimed* — rather than a restatement of the
arithmetic.
