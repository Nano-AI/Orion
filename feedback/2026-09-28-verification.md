# Integrated verification — 2026-09-28

Fresh `cmake --build build -j2` at `c31c964` after merging upstream
`bda5847` succeeded. All nine required gates passed after correcting one
agent-gate fixture. Logs are under `/tmp/orion-gates-20260928/`.

| Gate | Result |
|---|---|
| `orion-tests` | 1,143 checks, 0 failures |
| `orion-viewport-tests` | 4,269 checks, 0 failures |
| `check-decisions.py` | 284 rows, 1–286 with three declared gaps; all references resolve |
| `check-gestures.py` | 6 gestures accounted for |
| `check-screens.py` | 3 asserting scenes and 1 byte-stable scene passed |
| `check-modes.py` | library 13 checks; batch 2 files; HDR 243 MB DNG passed |
| `check-wiring.py` | 491 product functions swept; 8 harness-only accounted for |
| `check-agent.py` | 21/21 passed on rerun after fixing `mask_invert`'s fixture |
| `check-site.py` | all assertions passed |

The first agent run failed `mask_invert`: its chosen RAW had a near-black corner
(`0.0018` luma), so a 50% drop was an unsuitable oracle there. The check now
uses a 64×64 DNG made with Orion's existing `DngWriter`, requires measurable,
unclipped baseline regions, and retains all four mask assertions. In the final
run, centre luma fell `0.7087→0.1549` with ordinary coverage and corner luma
fell `0.7089→0.1549` with inversion. Forcing inversion either always false or
always true fails the check. The initial failure is in `agent.log`; the final
21/21 pass is in `agent-fixed.log`. The static masking audit's eight findings
remain source-traced, not render reproductions.

During app-launch checks, sampled free memory stayed at least 24%; a screen
scene process reached about 3.2 GiB RSS. No full-resolution 42 MP filter stress
run was repeated. The build warns that Homebrew LibRaw/OpenCV dylibs target
macOS 26 while Orion targets macOS 14; this run does not validate macOS 14.
The reduced-resolution cache result in the companion memory report is texture
payload from a controlled no-shrink mutation, not a whole-app 42 MP RAM result.
