# Orion design direction

`tokens.json` stays the single source of truth for values. This file records
the direction those values serve and the changes the redesign asks of them.
Canvas: the "Orion Develop Redesign" design artifact (Develop, Library,
Controls and states boards). Committed 2026-10-27.

## Discovery

| | |
|---|---|
| Artifact | Pro desktop tool: a RAW editor, dense, used for hours |
| Who | Photographers judging color and tone; one person at a keyboard |
| Primary job | See the photo truly, change it precisely, move to the next one fast |
| Adjectives | Precise, quiet, instrument-like, fast, honest |
| Essence | Darkroom instrument |

## Commitments

| Axis | Choice | Why |
|---|---|---|
| Neutrals | `tokens.json` greys unchanged, near-perfectly neutral | A tinted grey is a color cast in a raw editor (tokens.json's own rule) |
| Accent | `accent` role, cyan #4DB6C4 in both palettes, **interaction only**: edited value, slider fill, focus ring, pressed toggle, primary action. The edited dot is `status` | Cyan sits outside the indigo band and away from skin, and it is Orion's colour: H-beta, the nebula's blue-green line. ⚠ Measured 2026-10-30, it is 18° of OKLCH hue from a clear sky (#381). See Brand palette |
| Words | System face (SF Pro on macOS, Segoe UI on Windows) | Native controls read as native; Apple HIG. Deliberate override of the deslop skill's ban on system type |
| Numbers | Barlow Semi Condensed 500, tabular | Every figure looks read off a dial; condensed fits five-digit Kelvin in a 54 px readout. SIL OFL 1.1, fine to bundle in a proprietary app |
| Case | Sentence case everywhere; no tracked all-caps labels | Removes the most common generated-UI tell |
| Signature move | **Light-meter scale sliders**: tracks marked in real units (stops, Kelvin, degrees), the edit fills from the default rather than from the left end | Says what a slider does in the photographer's units, and shows at a glance what has been edited |

## Brand palette (2026-10-30, #383)

**To swap:** set `activePalette` in `tokens.json` to `hubble` or `belt`, run `design/build-tokens.py`; the app (`Palette.g.cs`) and the site (`web/css/tokens.css`) both follow, and the build fails, writing nothing, if any palette misses a bar below.

"Hubble" is the active palette: Orion's colours as the nebula and its stars give them.

| Slot | Hex | OKLCH | Role in `hubble` | Source |
|---|---|---|---|---|
| cyan | #4DB6C4 | 0.721 0.098 207 | `accent`, `mark` | The app's accent since the first tokens; H-beta (486.1 nm) renders #00B9C1, hue 200 |
| gold | #EEBB58 | 0.819 0.130 82 | `status`: ratings, warnings, the edited dot | The warm second colour. ⚠ The app's stars and warnings were #F0C674, ΔE<sub>OK</sub> 3.3 away; one source means they take #EEBB58 now |
| star-white | #E2EEFF | 0.945 0.026 257 | `gradient` only (`status` in `belt`) | Blackbody colour of the belt stars, 27-29.5 kK (Alnitak O9.5, Alnilam B0, Mintaka O9), CIE 1931 2° observer |
| deep | #166069 | 0.450 0.070 207 | `accentInk` (5.66:1 on the research paper), dark fills | Cyan's hue at L 0.45 |

`belt` is cyan and star-white only: `status` takes star-white where `hubble` has gold, and nothing else changes. Neutrals are in no palette: a tinted grey is a colour cast in a raw editor.

| Role | Means | Bar the build enforces (app greys and site greys, every palette) |
|---|---|---|
| `accent` | interaction only: an edited value, a slider's fill, the focus ring, the primary button | 4.5:1 as text on ground, panel, raised; 3:1 as a ring on ground |
| `status` | ratings, warnings, the edited dot | 4.5:1 on panel (a warning carries words) |
| `mark` | the belt logo, the sky's belt, the social card's belt | 3:1 on ground |
| `onAccent` | words on an accent fill (names a neutral: `ground`) | 4.5:1 on the accent |
| `accentInk` | the accent on the research pages' paper | 4.5:1 on #E4E4E1 |
| `gradient` | marketing only; nothing draws one today | none |

**Never pair cyan with red or pink.** H-alpha (656.3 nm), the nebula's other line, renders #FF4356: TikTok's red (#FE2C55, hue 18.7). The build refuses a palette with a slot of chroma > 0.05 within 25° of that hue beside a cyan. The reject flag (#C4574D) and the histogram's red channel are data colours outside the palette and sit beside the cyan in the app; neither is a brand pair.

The logo files (`web/img/orion-mark.svg`, `orion-icon.svg`) draw the mark in their own cyan gradients and do not follow `mark`: an SVG favicon cannot read the page's variables.

## Proposed token changes (stage 2 applies these through `build-tokens.py`)

| Token | Now | Proposed | Reason |
|---|---|---|---|
| `type.base` | 12 | 13 | macOS body size; 12 read cramped at panel density (still open) |
| `type.xs` | 10 | retire for figures | No numeral under 11 px (done; xs stays for the old mockup only) |
| `type.numeral` | none | Barlow Semi Condensed 500 | The numbers face above (done) |
| `trackingWide` / `trackingLabel` | used for caps labels | drop with the caps labels | Sentence case (done 2026-10-30) |

## Scales (token pass, 2026-10-30, #381)

Nothing in `shell/` sets a colour, spacing, radius, font size, shadow or icon size except through `tokens.json`.

| Axis | Scale | Rule |
|---|---|---|
| Space | 2 · 4 · 6 · 10 · 14 · 18 · 26 (`xs gap sm md pad lg xl`) | Tight inside a group (xs–sm), generous between groups (pad–xl). A control's side padding is `md`; icon to word is `sm`; a label to its control is `gap`; sheets and canvas pills sit `lg` in |
| Type | 11 · 12 · 13, then 17 · 28 (`Faces.Mark Label Body`, `Title`, `Display`) | 1 px steps for the working text; about 1.3× for the two large words |
| Radius | 3 · 5 · 7 | `lg` = `md` + 2: a well holding `md` controls 2 px in, so corners are concentric. A pill is half its height |
| Shadow | one: black .7, y 1, blur 3 | Only under what a hand grabs (thumb, puck, compare handle); hover and press move it from there |
| Icons | 24-unit grid kept, one 1.6 stroke, `sm` 12 / `md` 16 | Never stretched to a glyph's bounds; `sm` in a row of text, `md` on a button, tab or nameplate |
| Colour roles | `warn` apart from `star`; `faint` not for words | A warning is not a rating; faint is 2.5:1 |
| Exempt | 1 px hairlines, ring offsets, the 80 px traffic lights, pills, the tab cells' 8 px | The tab padding is pinned by `SelfTest.Design` (8 spare at 322, equal cells at 560) |

## Craft decisions

| Area | Decision |
|---|---|
| Layout | Tool column 322–560 px, resizable by a splitter on the hairline (canvas keeps 360); canvas never wraps under it (a wrapping row made the photo 1506 px tall); single column under 760 px |
| Overlays | Zoom and rating float over the photo as translucent pills; solid under reduced transparency |
| Tabs | Selected tab changes color only, never weight; a sliding indicator moves under it |
| States | Every control drawn at rest, hover, pressed (scale .97, 100 ms), focus, disabled, edited; canvas has loading, not-cached and couldn't-open states |
| Errors | Say what failed and offer Try again / Skip; arrows keep browsing |
| Focus | Two-ring box-shadow (ground, then accent) on buttons; outline on range thumbs |
| Motion | Critically damped springs for what the hand moves, ease-out under 200 ms for state marks; a cross-fade under reduced motion; nothing animates on the arrow-key path. See Motion |
| Shortcuts | Shown as keycaps in the footer, not as tooltips only |
| Imagery | Only real engine renders of the sample RAWs. The one pattern: Book of Shapes' Node Garden, at near-zero contrast behind the empty window and on the social card (#383) |

## Motion (2026-10-28)

The instrument answers the hand and shows what changed; nothing moves on its
own. The model is Apple's fluid interfaces (WWDC 2018): respond on pointer-down,
start every animation from the on-screen value, critically damped springs for
anything a person moves, the hand's velocity handed over on release, rubber
bands at the ends. Emil Kowalski's frequency rule decides what moves at all.
Values live in `Shared/Design/Motion.cs`; the one spring every control shares is
`Shared/Design/Spring.cs`, stepped on `TopLevel.RequestAnimationFrame`.

| Token | Value | Use |
|---|---|---|
| `Settle` | spring, damping 1.0, response 0.3 s: 90% at 185 ms, within 1% at 315 ms, the rest under a pixel | a value set from outside a drag; the thumb back from past an end |
| `Touch` | spring, damping 1.0, response 0.15 s: 95% at 115 ms | thumb hover lift and press |
| `Fade` | 120 ms opacity cross-fade | what `Settle` becomes under reduced motion |
| `Enter` / `Exit` | 160 / 100 ms, ease-out `cubic-bezier(0.23, 1, 0.32, 1)` | the edited dot arriving / leaving |
| `Band` | Apple's rubber band, constant 0.55, never past one thumb width (12 px) | the thumb pulled past an end |
| Button press | scale .97, 100 ms ease-out (unchanged) | `StateButton` |

| Moment | Motion | Purpose |
|---|---|---|
| A value changes from outside a drag: Auto, undo, redo, reset all, a preset, a typed value, a double-click reset | Thumb and fill glide on `Settle` from where they are on screen, at the speed they have; a second change re-aims mid-flight. The readout shows the new number at once | Shows which controls moved and how far; the number stays the truth |
| Pointer over a slider row (name, readout or track) | Thumb grows 12% and lifts (shadow offset 1 → 1.5, blur 3 → 5); the groove's edge brightens; the ticks and numeral within 24 px of the thumb light | Which control the hand is on, and the meter read where its needle is |
| Grab | On pointer-down the thumb presses back to +6% and its shadow tightens | The press registered |
| Drag | 1:1. Past an end the value holds and the thumb stretches against the band | Direct manipulation; a soft end instead of a wall |
| Release past an end | The thumb springs back on `Settle`, starting at the hand's release velocity | No seam between hand and spring |
| Release in range | Nothing: the value stays where the hand left it | A precision instrument does not throw values |
| Grab mid-glide | Caught where it is on screen; let go unmoved, the glide finishes | Every animation is interruptible |
| A section's first edit, or its last reset | The edited dot grows from half size as it fades in (`Enter`), and leaves on `Exit` | State, at the moment it changes |
| A tinted track (temperature, tint, HSL) | The edited span from the detent is drawn deeper (saturation ×1.35, brightness ×1.25) | The signature fill-from-the-detent reads on colour tracks too |

**Never animates.** The arrow-key path: a photo opening shows every value and
dot at once, a glide in flight included. A drag. The readout's number. Typing.

**Nothing waits.** A click on the readout types at once; the reset is a
double-click on the track. (A click on the readout used to reset after waiting
250 ms to rule out a double-click.)

**Reduced motion** (macOS Reduce motion, Windows "Show animations" off; read as
each animation starts): `Settle` becomes a 120 ms cross-fade, no rubber band,
the dot fades without growing. Hover and press still change the thumb.

**Cost.** The track draws itself, so the thumb and fill move by redrawing that
one control, with no layout pass: the same work as a drag frame. The dot and
the buttons animate `RenderTransform` and `Opacity`.

| Rejected | Why |
|---|---|
| The readout counting through the glide | The number is the truth; a rolling figure hides it |
| A hover state and disclosure motion on section headers | Plates do not collapse; a hover on something with no action is a false affordance, and collapsing is a feature, not motion |
| Momentum: a flicked thumb travelling on | The value would lead the finger |
| Bounce on the glide or the band | Nothing was flung |
| A glow or pulse on edited sliders | Ambient motion |
| Lighting the whole scale on hover | Forty lit rows is noise; only the marks at the thumb |

## Slop audit (2026-10-27)

| Tell | Result |
|---|---|
| Weight change on selected tab | Fixed: color only |
| Numerals under 11 px (scale marks were 10.5) | Fixed: 11 px minimum, half-pixel sizes rounded up |
| Focus by outline on rounded buttons | Fixed: box-shadow rings |
| No designed error state | Fixed: couldn't-open state added |
| Caps eyebrows, middle-dot meta strings, `→` links, gradient text, glows | None present |
| Indigo / cream / acid-green defaults | None: neutral greys plus cyan, gold for status |
| System font as primary face | Kept on purpose, see Commitments |
