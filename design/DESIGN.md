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
| Motion | Ease-out, under 200 ms, transform and opacity only; none under reduced motion; nothing animates on the arrow-key path |
| Shortcuts | Shown as keycaps in the footer, not as tooltips only |
| Imagery | Only real engine renders of the sample RAWs |

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
