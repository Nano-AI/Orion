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

## Looking, and what a proxy cannot show you

`get_proxy`'s `maxPx` stops at 2048.
On a 42 MP frame that is about a twentieth of the width, so **sharpening, skin texture, chromatic fringing and the edge of a mask are all finer than any whole-frame proxy you can ask for**.
A model that reads one and says "the sharpening looks fine" is reading a picture that does not contain the answer.

`region: [x, y, w, h]` is the fix.
It renders at full resolution, crops to that rectangle, and only then scales down if the crop's long edge is over `maxPx` - so a small region comes back at native pixels.
`get_stats` takes the same `region` and answers in numbers instead.

The order that costs the least:

1. `get_stats` whole-frame for the shape of the light.
2. `get_stats` with `region` on the two or three patches the decision turns on - a sky, a face, a shadow.
   A region reading costs a few tokens; a proxy costs hundreds.
3. `propose_edit`.
4. `get_stats` with `state: "proposed"` and the same regions, to find out whether the edit did what you meant **before** rendering anything.
5. `get_proxy` at 2048 with `state: "proposed"` for the whole picture, and a `region` zoom of any mask edge you created and of skin.
6. Say what you changed. The photographer approves in Orion.

### The numbers a region gives you

`luma`, `saturation`, `red`/`green`/`blue` are means over the patch.
`hue` is in degrees (0 red, 120 green, 240 blue) with `hueStrength` saying how much the patch agrees about it - near zero, the hue means nothing.

`clippedHigh` and `clippedLow` are the fraction of pixels at 254/255 and at 1/255.
A sky above about 0.01 `clippedHigh` is gone, not recoverable: there is no detail under the white to bring back, and pulling exposure there gives you grey rather than cloud.

`shading` is the standard deviation over the mean of block means - how much *modelling* the patch has, its light and shade rather than its pores.
It is the number that says whether a face went flat.
Measure a cheek before and after a strong Highlights pull or Shadows lift and **keep it within about 15% of as shot**; 40% of it going missing is what `highlights -0.8` did to a face under process 1 (`research/tone-and-local-contrast.md`).

### Local adjustments are not global ones at the same number

A masked exposure runs about three to four times a global's at the top of the scale.
That cuts both ways: a blown sky at luma 0.98 barely moves under a small masked pull, and the same value that looks timid on a sky is heavy-handed on a face.
**Probe strong and ease off** rather than creeping up: propose -2 EV, read `get_stats` with `state: "proposed"` on the patch, and come back to the number that lands.

### Masks: frame space is not display space

Every `maskComponents` coordinate, radius and length is in **frame** space - the sensor's un-turned frame, before crop, straighten, rotation and perspective.
Every `region` you pass to `get_proxy` or `get_stats`, and every box a proxy shows you, is in **display** space.
They are not the same rectangle, and on a turned photograph they are not even the same axes: a portrait frame off a landscape sensor puts frame-*top* down the display's *left* edge.

So:

- `detect_faces` gives both - the display box and the frame-space `centerX`/`centerY`/`radiusX`/`radiusY` that paste straight into a kind-2 radial. **Call it first on a portrait**; it is how a mask lands on the face rather than in the middle of the frame. Vision jitters by about 0.01 between calls, so do not read it to more than two decimals.
- For a sky, a kind-1 **linear gradient** row is what you want: the ramp is centred on `centerX`/`centerY` and covers the side `angle` points to (0 the frame's right, +1.571 rad the frame's bottom, -1.571 rad the frame's top, y downward), and `length` is how far the transition runs. Because of the turn above, **propose it and then probe both edges with `get_stats`' `region`** before believing which way it went.
- `invert` flips that component's own coverage before it folds into the group, so an inverted radial covers everything *outside* its ellipse. This is pinned on a real render in `tools/check-agent.py`: a radial with `invert: false` and a -3 EV layer darkens the centre and leaves the corner alone; `invert: true` does the exact opposite.
- `layers[i]` holds the adjustments for the i-th mask *layer*, not the i-th component. Row 0 always begins layer 0; every later component with `startsLayer` begins the next.

`describe_edits` carries all of this per key, including which fields each `kind` actually reads - a linear gradient ignores `feather`, a radial does read `angle` - so read it rather than guessing from the field list.

## Claude Code `.mcp.json`

```json
{ "mcpServers": { "orion": { "command": "node", "args": ["/ABS/PATH/Orion/mcp/server.ts"] } } }
```

Approve/deny for every edit happens in the MCP client's chat — this server
never writes a sidecar itself; only `approve_edit` does, and only on request.
