# Orion agent surface: `--agent` CLI and MCP server (POC)

⚠ **Historical.** The POC as specified on 2026-09-13. The living description of the
surface is `mcp/README.md`; since then `describe_edits`/`keys` (#247), `current_photo`
(#249), composites (#250), and `get_stats` with `state`/`region`, `get_proxy region` and
`detect_faces` (#281) were added, so the verb and tool tables below are incomplete.

Date: 2026-09-13. Status: approved by the developer in chat; proprietary license
chosen in the same conversation.

## Purpose

Let an agent (Claude Code, or any MCP client) cull and edit a folder of RAW
photographs through Orion **without the RAW ever reaching the model**. The
model sees small proxies and numbers; it returns partial edits; nothing touches
the photographer's sidecar until a human approves. The POC exists to measure
one thing: how long an agent takes to cull a real shoot against by hand.

## Non-goals (POC)

- No chat window inside Orion. Approve/deny happens in the MCP client's chat.
- No Apple Vision culling, no local models.
- No new engine or shader code. Every verb reuses an existing Swift entry point.
- No XMP knowledge outside Swift. The MCP server never parses a sidecar.

## Part A: `Orion --agent <verb> …`

A fifth command-line mode, dispatched in `OrionApp.init` exactly like
`--batch-export`, implemented in one new file `app/AgentCLI.swift` modelled on
`app/BatchExportDriver.swift` (accessory activation policy, `Engine()`, exit
codes). All output is UTF-8 JSON on stdout, one object; errors are one line on
stderr and exit 2 (usage) or 1 (failure). Exit 0 only on success.

Paths are absolute. `<raw>` is a RAW file Orion can open.

| Verb | Reads | Writes | stdout |
|---|---|---|---|
| `stats <raw>` | `orion_read_info`, `Sidecar.read(for:)`, `Engine.histogram(bins: 128)` after `open` + sidecar restore | nothing | `{"path","width","height","camera","rating","rejected","clipLow":[r,g,b],"clipHigh":[r,g,b],"mean":[r,g,b]}` where clip values are the share of pixels in the first/last bin per channel and mean is the histogram-weighted mean bin / 127, each 0…1 |
| `proxy <raw> --max <px> --out <jpg> [--state <json>]` | opens raw, restores sidecar develop state, then `Engine.restore(encoded:)` from `--state` if given | the JPEG at `--out` via `Engine.export(to:quality:0.85 maxDimension:px depth:8)` | `{"path":out,"width","height","bytes"}` |
| `apply <raw> --edits <json-file> --out <json-file>` | base state = `--state` if given, else the sidecar's develop state, else the engine default after open | `--out`: the full `DevelopState` JSON-encoded exactly as `Autosave.toSidecar` encodes it | `{"path":out,"changed":[keys]}` |
| `commit <raw> --state <json-file>` | the state file | the real sidecar via `Autosave.toSidecar(url, state)` | `{"sidecar":path}` |
| `flag <raw> [--rating 0-5] [--reject 0\|1]` | | the real sidecar via `Sidecar.merge(into:)`; reject 1 also sets rating 0, rating > 0 clears reject (same rules as `Library.setRating` / `setRejected`) | `{"rating","rejected"}` |

`apply` merges by round-tripping through JSON: encode the base `DevelopState`
to `[String: Any]`, overlay every key in the edits file, decode back. A key not
in `DevelopState` is an error (exit 2) whose message lists the allowed keys, so
a model that invents `exposure` instead of `exposureEv` learns the vocabulary
from the failure. `apply` never writes a sidecar.

`stats` and `proxy` need a GPU and a render; `apply`, `commit` and `flag` need
only `Engine` for the default state (`apply`) or nothing at all.

## Part B: `mcp/` server

TypeScript on `@modelcontextprotocol/sdk` (stdio transport), run directly by
Node 25 with native type stripping: `node mcp/server.ts`. No build step, no
bundler. Dependencies: the SDK and `zod` only. The Orion binary is
`ORION_BIN`, defaulting to `<repo>/build/Orion.app/Contents/MacOS/Orion`.

Each tool is one `child_process.execFile` of the binary. A proposed edit is a
file **beside the RAW**: `<raw stem>.proposed.json`, the output of `apply`.

| Tool | Input | Does | Returns |
|---|---|---|---|
| `list_folder` | `folder` | lists files whose extension is in `.arw .nef .cr2 .cr3 .dng .raf .orf .rw2` (case-insensitive) | `[{path, hasProposed}]` text JSON |
| `get_stats` | `path` | `stats` | the stats JSON |
| `get_proxy` | `path`, `maxPx` (default 1024, max 2048), `state` (`"current"` default or `"proposed"`) | `proxy` into a temp file, `--state` when proposed and the file exists | one `image` content block, `image/jpeg`, base64 |
| `propose_edit` | `path`, `edits` (object) | `apply` with `--state` = existing proposed file if present, writes the proposed file | the merged state JSON and `changed` |
| `approve_edit` | `path` | `commit --state <proposed>` then deletes the proposed file | `{"sidecar"}` |
| `reject_edit` | `path` | deletes the proposed file | `{"deleted": true\|false}` |
| `set_flag` | `path`, `rating?`, `reject?` | `flag` | the flag JSON |

Errors from the binary become MCP tool errors carrying the stderr line. Every
tool description states its token cost ("a 512 px proxy is about 220 tokens")
so the model reaches for `get_stats` before `get_proxy`.

Unit test: `mcp/server.test.ts` under `node --test`, using the SDK's
in-memory transport and `ORION_BIN` pointed at `mcp/test/fake-orion.sh`, a
shell stub that answers each verb with canned JSON and copies
`mcp/test/fixture.jpg` for `proxy`. It proves the wiring, the proposed-file
lifecycle and error mapping without a GPU.

## Part C: the gate, `tools/check-agent.py`

The eighth gate, in the style of `tools/check-modes.py`. Copies
`samples/_PIC8095.ARW` (and its sidecar if one exists) into a temp directory so
`samples/` is never written. Starts `node mcp/server.ts` on stdio and speaks
JSON-RPC by hand (no client library): `initialize`, `tools/list`, then:

1. `get_stats` → `width > 0`, `rating == 0`.
2. `get_proxy maxPx 512` → base64 decodes to bytes starting `FF D8`, long edge ≤ 512.
3. `propose_edit {exposureEv: 1.0}` → the proposed file exists and holds `"exposureEv":1`.
4. `get_proxy state proposed` → succeeds.
5. `approve_edit` → the sidecar exists and contains the encoded state with `exposureEv` 1; proposed file gone.
6. `set_flag rating 4` → `get_stats` reports `rating 4`.
7. `reject_edit` on a photo with no proposal → `deleted false`, no error.

Timeout per call 60 s, whole run 5 min. Requires a built Orion and GPU; prints
the "Build first" line the other gates print.

## Decisions to log (agent 5 writes these, numbered here to avoid collisions)

- **#243** License changed to proprietary for all commits from 2026-09-13; commits up to `abf3a51` remain Apache-2.0 and cannot be recalled. `NOTICE` keeps every third-party attribution.
- **#244** A fifth command-line mode, `--agent`, is the whole agent surface; JSON out, one file, no engine change.
- **#245** The MCP server is a thin TypeScript shell over the binary; no XMP, no Metal, no state of its own beyond a proposed file beside the RAW.
- **#246** A proposed edit is a `DevelopState` JSON beside the RAW, not a sidecar; only `commit` writes the sidecar, and only a human calls it.

## Global constraints

- macOS 14 floor. No Rust. No new engine dependencies. No GPL code.
- Files stay small: `AgentCLI.swift` under 300 lines; `mcp/server.ts` under 250.
- Build and run binaries with the sandbox off (Swift macro server dies inside it).
- Commit with explicit paths only, never `git add -A`; two agents share the tree.
- Run all seven existing gates before claiming Part A works; the eighth after Part C.
