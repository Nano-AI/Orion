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
| Accent | `accent` #4DB6C4, **interaction only**: edited fill, pressed toggle, focus ring, primary action | Cyan sits outside the indigo band and away from skin and sky hues |
| Words | System face (SF Pro on macOS, Segoe UI on Windows) | Native controls read as native; Apple HIG. Deliberate override of the deslop skill's ban on system type |
| Numbers | Barlow Semi Condensed 500, tabular | Every figure looks read off a dial; condensed fits five-digit Kelvin in a 54 px readout. SIL OFL 1.1, fine to bundle in a proprietary app |
| Case | Sentence case everywhere; no tracked all-caps labels | Removes the most common generated-UI tell |
| Signature move | **Light-meter scale sliders**: tracks marked in real units (stops, Kelvin, degrees), the edit fills from the default rather than from the left end | Says what a slider does in the photographer's units, and shows at a glance what has been edited |

## Proposed token changes (stage 2 applies these through `build-tokens.py`)

| Token | Now | Proposed | Reason |
|---|---|---|---|
| `type.base` | 12 | 13 | macOS body size; 12 read cramped at panel density |
| `type.xs` | 10 | retire for figures | No numeral under 11 px |
| `type.numeral` | none | Barlow Semi Condensed 500 | The numbers face above |
| `trackingWide` / `trackingLabel` | used for caps labels | drop with the caps labels | Sentence case |

## Craft decisions

| Area | Decision |
|---|---|
| Layout | Tool column 322 px fixed; canvas never wraps under it (a wrapping row made the photo 1506 px tall); single column under 760 px |
| Overlays | Zoom and rating float over the photo as translucent pills; solid under reduced transparency |
| Tabs | Selected tab changes color only, never weight; a sliding indicator moves under it |
| States | Every control drawn at rest, hover, pressed (scale .97, 100 ms), focus, disabled, edited; canvas has loading, not-cached and couldn't-open states |
| Errors | Say what failed and offer Try again / Skip; arrows keep browsing |
| Focus | Two-ring box-shadow (ground, then accent) on buttons; outline on range thumbs |
| Motion | Critically damped springs for what the hand moves, ease-out under 200 ms for state marks; a cross-fade under reduced motion; nothing animates on the arrow-key path. See Motion |
| Shortcuts | Shown as keycaps in the footer, not as tooltips only |
| Imagery | Only real engine renders of the sample RAWs |

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
| Indigo / cream / acid-green defaults | None: neutral greys plus cyan |
| System font as primary face | Kept on purpose, see Commitments |
