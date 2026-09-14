# Orion agent MCP server (POC)

Thin stdio MCP shell over `Orion --agent <verb>`. No XMP, no Metal, no state
of its own beyond a `.proposed.json` file beside each RAW. See
`docs/superpowers/specs/2026-09-13-agent-mcp-design.md`.

## Run

```bash
cd mcp && npm install
ORION_BIN=/path/to/Orion.app/Contents/MacOS/Orion node server.ts
```

`ORION_BIN` defaults to `<repo>/build/Orion.app/Contents/MacOS/Orion`.

## Test

```bash
cd mcp && npm test        # node --test against test/fake-orion.sh, no GPU needed
```

## The photo open in Orion

`current_photo` reads `~/Library/Application Support/Orion/current.json`
(path overridable with `ORION_CURRENT`, mainly for tests), which Orion
writes atomically on every photo change: `{"photo", "folder", "updated"}`.
Call it with no input to find out what the user has open before asking them
for a path. If nothing is open, or the file doesn't exist yet, it still
returns a normal (non-error) result: `{"photo":null,"hint":"open a photo in
Orion, or pass path explicitly"}`.

Every other tool's `path` (and `list_folder`'s `folder`) is **optional** and
defaults to this: omit it and the tool resolves from `current.json` through
one shared helper. If that also comes up empty, the call fails with
`no photo is open in Orion and no path was given` — pass a path explicitly
or tell the user to open a photo.

## How to edit safely

Values are **absolute settings, not deltas** — `temperatureK: 150` is not
"150 cooler", it is "150 kelvin", which is black. An earlier session on
Haiku learned this the hard way: it merged three "dial back" edits onto the
same poisoned proposed file and approved a black photo because nothing told
it otherwise. Given that:

1. Call `describe_edits` before your first `propose_edit` — it lists every
   key's unit, range and default.
2. Call `get_stats` to read the photo's *current* `temperatureK`/`tint`
   before changing white balance, so an edit is relative to the real value,
   not a guess.
3. `propose_edit` **accumulates**: a second call merges onto the existing
   proposed file, keeping everything the first call set. Pass `reset: true`
   to start over from the photo's current state instead.
4. **Composite controls** — `curve`, `gradeShadow`/`Midtone`/`Highlight`,
   `hueShift`/`satShift`/`lumShift`, `layers`, `spots`, `maskComponents` —
   are `describe_edits` entries of `type: "object"` or `"array"` with an
   `example` instead of a `min`/`max`. They replace **the whole value**, not
   individual elements: read the current one from a previous result's
   `state` or from `describe_edits`' `example`, edit it, and send it back
   complete. A partial curve (say, one point) is rejected, not merged onto
   the existing one.
5. Look at the proposed proxy (`get_proxy` with `state: "proposed"`) before
   calling `approve_edit`. Never approve a proxy you have not looked at.
6. A rejected edit comes back as a tool error whose text starts with
   `REJECTED: ` and names the valid range — read that as a rejection, not a
   rendering glitch.

## Claude Code `.mcp.json`

```json
{ "mcpServers": { "orion": { "command": "node", "args": ["/ABS/PATH/Orion/mcp/server.ts"] } } }
```

Approve/deny for every edit happens in the MCP client's chat — this server
never writes a sidecar itself; only `approve_edit` does, and only on request.
