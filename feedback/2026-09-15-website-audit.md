# Website audit, 2026-09-15

Branch: `web/site-audit-polish`. Local website review and targeted fixes.

| Finding | Fix |
|---|---|
| The head added `.js` before the menu script loaded. A failed script left a hidden menu with a dead toggle. | Enable the collapsed menu only after its handlers exist. The fallback is a scrollable second navigation row. |
| The mobile menu stayed open after clicking outside or moving keyboard focus away. Escape also focused the hidden toggle on desktop. | Close on outside pointer/focus and breakpoint changes; handle Escape only while open. |
| With scripts off, the exposure picture was −2 EV while the output said +0.40 EV. Sliders still looked interactive. | Default to the matching fifth render and hide unavailable controls. |
| Compare arrows advanced only 0.1 percentage points; Shift did nothing. End-position grips were half clipped. | One-point arrow steps, ten-point Shift steps, and grips held inside the image. |
| The exposure target was 18 px tall and Firefox lost its focus ring. | A 44 px target and focus styles for both range-thumb implementations. |
| Stacked papers covered later citation links. | “View all papers” releases the pin and presents the existing paper grid. Keyboard focus on a citation also expands it. |
| The hero offered only Contact and deferred development status to the bottom. | Add “Explore the editor” and a short macOS/development line. |
| Terminal labels used the faint decorative color; phone text dropped to 10.5 px. | Use the readable dim color and retain 11.5 px on phones. |

## Verification

- All nine repository checks pass: engine 1,034; viewport 4,210; agent 14/14, plus decisions, gestures, screens, modes, wiring and site.
- Headless Chrome through Playwright over localhost HTTP: widths 320, 390, 768, 1024 and 1440 at 900 px height; no horizontal page overflow or runtime errors. All image assets decoded successfully.
- Browser assertions: compare arrow/Shift keys; mobile menu open/Escape; reduced-motion mode; JavaScript disabled; site scripts blocked; animated paper stack expanded into eight papers.
- Desktop hero and paper-grid screenshots inspected. No deployment performed. Safari, Firefox and physical touch devices were not exercised; the Firefox focus fix is source-reviewed.

The existing photography, feature-register wording and download-withdrawal policy remain the basis of the page. The browser checks were run from a temporary Playwright script; they are not a new repository gate.
