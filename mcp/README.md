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
   To read every setting, call `get_edits`: it is read-only and never writes
   anything, returns `{path, source, state, hasProposed}` with `source` one
   of `"proposed"`, `"sidecar"` or `"as-shot"`, and with `state: "proposed"`
   reads the proposal so far instead of the current settings.
   Its `state` is the same full object `propose_edit` returns, so composite
   fields come back whole for a read-modify-write.
   Do not send an empty `propose_edit` to find out the settings - that
   writes a proposal the photographer then sees.
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

`noise` is fine grain: the RMS of luma against its own 3x3 mean, in `luma`'s units.
It is flat on a smooth wall and high on noise, and also high on fine texture, so read it **before and after on the same patch**, never across patches.
`shading` is the number a face keeps; `noise` is the number a wall loses.

### Noise

Measure `noise` on a flat patch - a wall, a sky, a backdrop - and `shading` on a face before proposing anything.
Then propose `denoiseLuma` for the grain, `denoiseColor` for the colour blotches, and `denoiseDetail` (0..1) where there is texture to keep.
Detail backs the threshold off where the picture has more texture than noise, and 0.5 is a fair start on rock and fabric.
Re-measure both under `state: "proposed"`: `noise` should fall on the wall and `shading` should hold on the face.

`denoiseColor` alone leaves luminance alone, so a colour-only pass to kill blotches no longer softens texture.

A layer carries `denoiseLuma` and `denoiseColor` too, in the same 0..4 units, applied through its coverage **on top of** the global chain.
A layer cannot un-denoise: the global chain has already run.
So to clean the wall and keep the climber, keep the global low and put the denoise on a layer with the subject subtracted (`compose: 1`) or on an inverted subject layer.

### Local adjustments are not global ones at the same number

A masked exposure runs about three to four times a global's at the top of the scale.
That cuts both ways: a blown sky at luma 0.98 barely moves under a small masked pull, and the same value that looks timid on a sky is heavy-handed on a face.
**Probe strong and ease off** rather than creeping up: propose -2 EV, read `get_stats` with `state: "proposed"` on the patch, and come back to the number that lands.

### Masks: frame space is not display space

Every `maskComponents` coordinate, radius and length is in **frame** space - the sensor's un-turned frame, before crop, straighten, rotation and perspective.
Every `region` you pass to `get_proxy` or `get_stats`, and every box a proxy shows you, is in **display** space.
They are not the same rectangle, and on a turned photograph they are not even the same axes: a portrait frame off a landscape sensor puts frame-*top* down the display's *left* edge.

So:

- `get_stats` with `region` gives both too: the region object carries `frame`, `{centerX, centerY, radiusX, radiusY}`, the same rectangle carried into frame space as the ellipse that covers it. Paste it straight into a kind-2 radial, or aim a kind-1 gradient's `centerX`/`centerY` at it; it is the same arithmetic `detect_faces` uses, so never convert a region to a mask by hand.
- `detect_faces` gives both - the display box and the frame-space `centerX`/`centerY`/`radiusX`/`radiusY` that paste straight into a kind-2 radial. **Call it first on a portrait**; it is how a mask lands on the face rather than in the middle of the frame. Vision jitters by about 0.01 between calls, so do not read it to more than two decimals.
- For a sky, a kind-1 **linear gradient** row is what you want: the ramp is centred on `centerX`/`centerY` and covers the side `angle` points to (0 the frame's right, +1.571 rad the frame's bottom, -1.571 rad the frame's top, y downward), and `length` is how far the transition runs. Because of the turn above, **propose it and then probe both edges with `get_stats`' `region`** before believing which way it went.
- `invert` flips that component's own coverage before it folds into the group, so an inverted radial covers everything *outside* its ellipse. This is pinned on a real render in `tools/check-agent.py`: a radial with `invert: false` and a -3 EV layer darkens the centre and leaves the corner alone; `invert: true` does the exact opposite.
- A radial's `feather` is the share of the radius the ramp takes, measured inward from the boundary: coverage is full inside (1 - feather) of the radius and zero at the edge, so at feather 0.8 the ellipse is fully on only across its inner 20%, and a compose-1 subtract row at 0.8 protects far less than its outline suggests. Size the ellipse to the subject plus the feather, or keep feather near 0.5 when the outline is the subject's own edge.
- The global `vignetteAmount` is centred on the composition, not the sensor frame: its circle is the crop's (decision #103), so it follows a crop or a straighten and its centre is the displayed picture's centre.
- `layers[i]` holds the adjustments for the i-th mask *layer*, not the i-th component. Row 0 always begins layer 0; every later component with `startsLayer` begins the next.

`describe_edits` carries all of this per key, including which fields each `kind` actually reads - a linear gradient ignores `feather`, a radial does read `angle` - so read it rather than guessing from the field list.

## Selecting the subject

`detect_subject` is the panel's Subject, Person and Sky button: the same Vision models, run on the photo as developed.
`kind: "subject"` lifts whatever stands out from the background, `"person"` is the person matte and `"sky"` the sky.
It writes the matte as a PNG beside the photo and returns `component`, a complete kind-4 `maskComponents` row.

- **Append** it: read the current `maskComponents` and `layers` with `get_edits`, add the row and a matching layer, and send both arrays back whole in `propose_edit`.
- The row's `startsLayer: true` begins a new layer. To take the subject *out* of the previous layer instead ("everything but the climber"), set `startsLayer: false` and `compose: 1` and add no layer.
- `found: false` is an answer, not an error: the model found nothing to select.
- `coverage` is the fraction of the frame selected. Near 0 or near 1 means the model grabbed the wrong thing, so look at an edge with `get_proxy`'s `region` before approving.
- The matte runs strong, like any local adjustment: probe strong and ease off with `get_stats` `state: "proposed"`.
- The PNG is kept as long as a proposal, sidecar or version names it.

## Applying one photo's edit to a shoot

The workflow for a shoot is to edit one photograph with the photographer until they approve it, then `sync_edits` from it to the rest.
It suits a set of frames of the same event in the same light, a climbing competition shot from one spot being the shape it was built for.
`list_folder` gives the target paths.

- `groups` defaults to `light`, `color`, `curve` and `effects`: the look. `whiteBalance` and `detail` belong to the camera and ISO of each frame, so they stay unless named.
- `masks` carries `maskComponents` and `layers`. Every Subject, Person and Sky row is re-detected on each target with a fresh matte, while gradients and radials copy by position in frame space, so check one or two targets with `get_stats`' `region` or `get_proxy` before approving.
- By default each target gets a proposal, which **replaces** any proposal already there. The photographer approves each in Orion, or asks for `approve_edit` by path.
- `commit: true` writes the sidecars directly and cannot be undone. Never pass it without the photographer saying so.
- **The open photo rule:** under `commit`, the photo open in Orion is still written as a proposal and named in `openPhotoProposed`. Orion holds that photo's state in memory, and its autosave would overwrite a sidecar written behind its back; a proposal shows up live in its compare view instead.
- A target that fails comes back in `results` with an `error`; the call itself still succeeds.

## Claude Code `.mcp.json`

```json
{ "mcpServers": { "orion": { "command": "node", "args": ["/ABS/PATH/Orion/mcp/server.ts"] } } }
```

Approve/deny for every edit happens in the MCP client's chat — this server
never writes a sidecar itself; only `approve_edit` does, and only on request.
