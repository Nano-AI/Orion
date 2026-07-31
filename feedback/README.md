# feedback/

Every critique of this repository, and the investigations that came out of them.

Kept together, and kept in the repository rather than in a chat log, for one
reason: **the criticism should be as findable as the plan.** `planning/` says
what Orion is meant to be; this says where it is not that yet, who noticed, and
what the measurement showed.

Nothing here is deleted when it is fixed. A finding that was closed still records
how a defect got past every test — which is usually the more useful half.

## Contents

| File | What it is |
|---|---|
| `2026-07-28-senior-review.md` | Outside senior review of the whole repository. 17 findings, ranked, each with file:line evidence and a concrete fix. The three P1s and finding 4 are closed; 6–14 are open. |
| `2026-07-28-performance-and-quality.md` | Self-assessment written for a reviewer who has not seen the repository: what to run, the latency table, how correctness is defended, and a plainly stated list of known weaknesses. Contains corrections to its own earlier claims. |
| `2026-07-28-colour-investigation.md` | The purple sky. Full thread: measurements against three independent renderers, what was ruled out and how, two outside AI reviews that corrected the approach, the licensing analysis, and what is still undecided. |
| `2026-07-28-colour-investigation-response.md` | Outside review of the colour investigation, with the web research the original session ran out of budget for. Confirms the diagnosis against the DNG spec and profiling literature; adds two findings the plan needed: the HueSatMap must apply in linear ProPhoto HSV, and the 1.3× darkness matches the DNG BaselineExposure mechanism. |

## How to read these

Start with the senior review — it is the widest net. The quality doc is the
self-assessment it was checking, including the places where the self-assessment
was too kind. The colour investigation is the deepest single thread and is the
best example of the pattern that keeps recurring here:

> The code was fine wherever it was measured.

Every serious defect in this project so far has lived in a state nobody pointed
an instrument at. A purple sky survived two test suites, a benchmark and a full
review because the entire sample corpus was two night frames.

## Where the fixes live

Findings are closed in code, and the reasoning is recorded in:

- `planning/DECISIONS.md` — every settled choice, numbered, with its reason
- `planning/HISTORY.md` — the archived session log, 50 sessions and counting
- `planning/STATUS.md` — current state plus the recent session log, including corrections to overstated
  commit messages
- `research/UNSOURCED.md` — the honest register of what is our own formulation
  rather than a published algorithm
- `research/*.md` — the algorithm sources themselves
