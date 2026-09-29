# Checker and calibration tools audit — 2026-09-28

Full source read of the 12 remaining Python/fixture tools (2,490 lines),
separate from product and test-suite counts. No application, compiler, GPU,
calibration sweep or test mutation ran while kernel memory pressure was 2.
These findings are source-derived; static ledger checks in earlier checkpoints
do not reproduce them. Packaging and worktree setup were already read in the
separate build-tooling audit.

| Priority | Concrete checker defect | Smallest correction and missing proof |
|---|---|---|
| P2 | **The MCP gate's timeout is not a hard deadline.** `check_agent_steps.py:57-70` waits for a readable pipe and then calls blocking `readline()`. A server that writes part of a line and stays alive can hang there indefinitely. A continuous stream of other response IDs also bypasses the deadline: `max(0.1, deadline-now)` never tests that time has expired when the pipe keeps being readable. | Use a monotonic deadline checked on every iteration and bounded/nonblocking line accumulation with stdlib I/O. A tiny fake child emitting an unfinished line, and another emitting unrelated IDs, should both finish as timeout failures. Neither fault was run here. |
| P2 | **The sample-preservation assertion cannot see ignored sample changes.** `check-agent.py:132-137` uses ordinary `git status --porcelain samples/` as its only post-run preservation check, while `.gitignore:14` ignores `samples/`. Replacing a sidecar or an untracked RAW there leaves that assertion empty; changing a linked original does not change a tracked symlink either. Current steps use temporary copies, which is useful isolation, but the named guard cannot detect the regression it claims to assert. | Compare an explicit before/after snapshot of the source fixture and relevant sibling files, using streamed hashes where necessary. Exercise a deliberate change only inside an isolated fake sample directory. Do not mutate the photographer's files to test the guard. |
| P2 | **A calibration check can pass with no measurable color bands.** `huesatfit.py:360` uses a default worst error of zero when every band is below `MIN_PIXELS`; `:361-368` then prints success. A blank or sufficiently gray corpus can therefore report that the compiled curve is within tolerance while providing no eligible band evidence. `huefit.py:77-78` also omits the two patches labeled “must not move” from its error calculation, so their movement cannot fail that check. | Require nonempty finite measured-band evidence and report unsupported bands explicitly; assert the two control patches against an appropriate identity/reference measurement. Pure synthetic measurement rows can pin empty/nonfinite rejection without a renderer. |
| P2 | **Failed conversion can reuse a stale calibration result.** `huesatfit.py:262-266` writes each iteration to the same `*.ori.png`, ignores `sips`' exit status and immediately returns that path. If a later conversion fails, an earlier PNG remains readable and is measured as the new render. Camera PNGs are likewise cached only by basename/existence (`:236-250`), without a source-file stamp. | Check subprocess exit statuses and publish each conversion only after success; use a private fresh output or remove only the owned previous result before conversion. Key the camera cache on the RAW's identity/stamp. A failing conversion stub must not leave a successful measurement of old bytes. |
| P2 | **The profile-fitting corpus is not guaranteed to be as-shot.** The `huesatfit.py` header assumes no sidecars exist, but `render_corpus` passes original RAW paths directly to normal `--batch-export` (`:232-258`). That driver reads saved edits (`app/BatchExportDriver.swift:111`). A later edit beside a corpus RAW changes the measured profile correction while the embedded-camera reference stays unchanged. | Refuse an edited corpus or stage RAW links in a private sidecar-free directory, reusing the repository's existing isolated-fixture pattern. A nondefault sidecar must trigger refusal or demonstrably leave the calibration baseline unchanged. No corpus run was made. |
| P3 | **Decision-reference checking omits current audit sources and suffix identity.** `check-decisions.py:47-48` does not scan `feedback/` or MCP source/docs, and `:54,152-159` reduces a reference to its numeric prefix. A nonexistent letter-suffixed identifier can resolve merely because its numbered base exists. The duplicate-row check already distinguishes suffixes, so the two halves use different identity rules. | Include maintained first-party decision-bearing directories and resolve optional suffixes against the existing `(number, suffix)` keys, preserving the documented subsection and hex-color exclusions. Use temporary text fixtures for a missing audit reference and an unknown suffix. Do not plant bogus references in recovery memory. |

Process cleanup is another **unverified resource boundary**:
`check-agent.py:101-107` and `check_agent_steps.py:483-489` terminate the Node
server only. The server starts Orion using `execFile` without a timeout or
recorded process-group cleanup (`mcp/server.ts:29-38`). A gate timeout has no
explicit mechanism here to terminate a still-running renderer descendant.
No orphan was observed in this pass; a fake long-running child can check that
boundary without invoking Metal. The unread `stderr` pipe in `spawn` is also a
possible backpressure path, not a measured production hang.

Useful existing protections remain: mode/screenshot gates use subprocess
timeouts; batch and merge outputs go in `TemporaryDirectory`; the inversion
check uses the small generated DNG and asserts usable baseline luma before its
four actual GPU effects; matte checks pin both upload and missing-file refusal.
Gesture/wiring gates openly state their regex limits, so this audit does not
treat them as physical interaction or compiler coverage. Website checks do not
fetch links or drive a browser, as their docstring states.

| Full file read (`tools/`) | Lines | Full file read (`tools/`) | Lines |
|---|---:|---|---:|
| `check-agent.py` | 150 | `check_agent_steps.py` | 509 |
| `check-decisions.py` | 195 | `check-gestures.py` | 122 |
| `check-modes.py` | 220 | `check-screens.py` | 200 |
| `check-site.py` | 297 | `check-wiring.py` | 247 |
| `huefit.py` | 130 | `huesatfit.py` | 376 |
| `build-planet-maps.py` | 21 | `fixtures/mask-invert.cpp` | 23 |

Supporting reads were limited to `.gitignore`, the current batch driver's
saved-edit boundary, and MCP child launch. No mathematical profile change is
proposed; the calibration findings concern input identity and the validity of
its reported evidence. All 14 tracked source tools now have documented full
reads across this and the build-tooling report; binary fixtures were not
regenerated or relabeled as reviewed source.
