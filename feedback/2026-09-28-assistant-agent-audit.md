# Assistant and agent surface audit — 2026-09-28

Source-only review at the current worktree. No build, compiler, app launch, GPU render, network call, or test was run; kernel memory pressure was at warning level 2. Prior desktop, masking, canvas, engine and batch findings in this directory remain separate and are not repeated here. In particular, the desktop audit already records one-renderer-per-tool RAM pressure and the full-frame materialization for a regional proxy.

## Exact full-read inventory

| Product file read in full | Lines |
|---|---:|
| `app/AgentCLI.swift` | 266 |
| `app/AgentComposite.swift` | 303 |
| `app/AgentFaces.swift` | 89 |
| `app/AgentInspect.swift` | 113 |
| `app/AgentVocabulary.swift` | 180 |
| `app/AssistantPanel.swift` | 278 |
| `app/AssistantProcess.swift` | 159 |
| `app/AgentCLIDriver.swift` | 261 |
| **Total** | **1,649** |

The first seven were previously unread in the coverage map; `AgentCLIDriver.swift` was already listed as a full read in the desktop audit. This pass adds seven new full product files to documented coverage. Targeted supporting reads: `mcp/server.ts` process/proposal/tool handlers, `app/Proposal.swift` path and transition rules, `app/ProposalWatcher.swift` file application, `app/EditHistory.swift` decode policy, `app/Sidecar.swift` strict export read, `app/AgentKeys.swift` scalar catalogue, and the vendored SwiftTerm start/terminate call sites. Test references were searched, not executed. Read `planning/STATUS.md` first, then relevant vision, architecture, M-A roadmap/features, decision rows #244–#251/#280–#285, and `feedback/README.md` and prior audit findings. The large historical planning files were searched for applicable decisions, not reread in full.

## Ranked findings

### 1. P1 — An ill-typed proposed edit can silently reset a saved control

**Trigger:** the current edit has `temperatureK: 7000`, then `propose_edit` receives `{"temperatureK":"warm"}`. `AgentCLI.mergeEdits` checks numeric range only if the value casts to `NSNumber` (`app/AgentCLI.swift:214–226`), then decodes with a normal `JSONDecoder` (`:228–237`). `DevelopState`'s normal decoder deliberately uses `try?` and the neutral default for a present invalid field (`app/EditHistory.swift:18–28`, `:681–692`). Thus the invalid value is accepted as a valid merged state containing `temperatureK: 5500`; the proposed file is written atomically and the returned `changed` list still names `temperatureK` (`app/AgentCLIDriver.swift:152–160`). The comment at `AgentCLI.swift:232–234` says such a value fails, but the chosen decoder does not do that. Any other scalar with a default has the same route. The app watcher also decodes proposals normally before applying them (`app/ProposalWatcher.swift:186–190`), so the visible preview can look like a real edit rather than an error.

**Consequence:** an agent typo may overwrite a prior adjustment with a default and present it for human approval. **Smallest fix:** use the existing `.strictDevelop` decoder mode (`app/Sidecar.swift:61–63`) at the `mergeEdits` decode, while retaining absent legacy fields. A single pure regression with a nondefault base and one present string numeric field should require rejection and byte-identical existing proposal output. Also cover a malformed nested field, because `AgentComposite` validates shape and ranges but the final decoder owns type fidelity.

### 2. P1 — `commit` can convert a malformed proposal into a whole default sidecar

**Trigger:** a proposal file contains valid JSON such as `{"temperatureK":"warm"}`, whether hand-edited, written by an older client, or supplied through the public `--agent commit --state` verb. `runCommit` uses the same forgiving `JSONDecoder` without `.strictDevelop` (`app/AgentCLIDriver.swift:165–172`), then calls `Autosave.toSidecar` with the decoded `DevelopState`. For this payload the decoder keeps default values for every absent field and the invalid temperature. The commit reports success and replaces the develop payload with a default state. `mcp/server.ts:300–307` passes the on-disk proposal directly to that verb and removes the proposal after success. This is distinct from finding 1 because fixing proposal creation alone leaves existing or externally written proposal files unsafe.

**Smallest fix:** share the existing strict decode policy at the sidecar write boundary and reject malformed or partial whole-state proposal files before `Autosave.toSidecar`. Require a complete state here, unlike legacy sidecar reads. Pin with one CLI check: malformed proposal, existing edited sidecar, `commit` exits nonzero and both files remain byte-identical.

### 3. P1 — RAWs with the same stem share one proposal file

**Trigger:** `shot.ARW` and `shot.DNG` coexist in one folder. Both `Proposal.proposedURL` (`app/Proposal.swift:23–31`) and MCP `proposedPath` (`mcp/server.ts:16`) strip the RAW extension, so both resolve to `shot.proposed.json`. A proposal for either photo is offered to the other's watcher on open (`app/ProposalWatcher.swift:49`, `:57–69`, `:124–132`), and `propose_edit` for the second photo may use the first photo's file as its base (`mcp/server.ts:273–281`). `approve_edit` then calls `commit` for whichever path was passed with that shared state (`mcp/server.ts:300–307`); `runCommit` does not bind the state to a photo (`app/AgentCLIDriver.swift:165–172`). This is a concrete wrong-photo edit path for a common RAW plus converted DNG pair.

**Smallest fix:** key proposals by the complete RAW filename in both existing path helpers and preserve the old-stem file only through an explicit migration/compatibility rule. Include one two-file path check and one open/propose/approve check that the second photo cannot adopt the first's edit. The existing MCP lifecycle test uses only one `shot` (`mcp/server.test.ts:80–95`).

### 4. P2 — A requested proposed-state inspection silently measures the current state when no proposal exists

`get_stats`, `get_proxy`, and `detect_faces` append `--state` only when the proposal file exists (`mcp/server.ts:175–178`, `:216–219`, `:360–363`). A caller that asks for `state: "proposed"` after rejection, failed creation, or a filename collision receives a successful current-state answer without a marker saying it changed subjects. That can make an agent conclude a proposed edit had no effect and retry it. The current test explicitly expects this fallback (`mcp/server.test.ts:245–261`), so this is a product-contract issue, not missing test coverage. **Smallest fix:** return a tool error when `state: proposed` is requested and the proposal is absent; the caller can ask for current explicitly.

### 5. P2 — CLI `flag` silently changes the requested rating

`AgentCLI.parse` turns an invalid `--rating` into `nil`, and any nonzero `--reject` into `true` (`app/AgentCLI.swift:155–158`). `runFlag` clamps even an otherwise valid integer to 0…5 (`app/AgentCLIDriver.swift:177–199`). Thus `--rating 9` succeeds as 5, and `--rating typo --reject 1` succeeds while dropping the rating argument. The MCP schema rejects these inputs (`mcp/server.ts:377`), but the public `--agent` CLI accepts them. **Smallest fix:** reject unparseable or out-of-range provided options in `parse`; do not clamp at the write boundary. One pure parse check covers both cases.

## Existing proof and limits

`tools/check-agent.py` last passed 21/21 on a real render at the earlier merged checkpoint; `mcp/server.test.ts` last passed 27/27, and assistant launch tests exercise the real login-shell PATH and argument quoting (decision #282). Those checks cover ordinary proposal/commit, valid numeric range refusal, region/faces payloads, and shell launch. They do not exercise malformed present values, partial `commit`, same-stem RAW pairs, or `state: proposed` without a proposal as a refusal. This report has no runtime reproduction and makes no new latency or RAM measurement. The prior desktop audit's renderer-process and regional-full-frame costs remain the performance findings to measure before adding another engine or cache.
