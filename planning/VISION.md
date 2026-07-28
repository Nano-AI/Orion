# Orion — Vision

**One-liner:** Fast, modern, subscription-free desktop RAW editor that makes dramatic ("180°") edits achievable by anyone.

## Problem
- Adobe Lightroom dominates RAW editing; subscription pricing locks out students and hobbyists.
- Alternatives are either expensive (Capture One), dated/complex (darktable, RawTherapee), or limited.

## Goals
1. **Fast** — GPU-accelerated pipeline, instant preview feedback, smooth on large RAW files.
2. **Powerful** — full RAW development toolset capable of transformative edits, not just tweaks.
3. **Usable** — modern, clean, minimalist UI; shallow learning curve.
4. **Owned** — one-time purchase or free; no subscription.

## Primary user
- The author (personal camera workflow) + students/hobbyists priced out of Adobe.

## Design references
- **Capture One** — strongest UI reference: restrained dark theme, tabbed tool groups, pro feel without clutter.
- Lightroom — workflow familiarity baseline (import → develop → export).

## Non-goals (initial)
- Cloud sync / mobile apps
- Full DAM (asset management) beyond basic library browsing
- Pixel-level retouching (clone/heal beyond basics), layers à la Photoshop

## Success criteria
- Import → edit → export a shoot start-to-finish without touching another tool.
- Preview slider latency imperceptible (<16 ms target for adjustment feedback).
- A beginner produces a striking edit within first session.
