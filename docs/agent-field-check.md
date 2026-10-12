# Field check for an editing session on the Orion MCP

Paste this into the model that edits with you.
It names the things that were built on 2026-10-17 without a hands-on pass, so the first real session is the test.
Send whatever it reports back to the Orion development session.

## While editing one photograph

1. `detect_subject`: call it with `kind: "person"` on a frame with a climber and with `kind: "subject"` on the same frame.
   Report the two `coverage` numbers and whether `found: false` ever came back on a frame that clearly has a person.
2. Append the returned `component` to `maskComponents` with a strong layer (`exposureEv: -2`), then read `get_stats` with a `region` on the climber and one on the wall, `state: "proposed"`.
   Report whether only the climber moved.
   If both moved or neither did, say which, with the two luma readings.
3. Look at one mask edge with `get_proxy` `region` at native pixels.
   Report whether the matte follows the body or the rope, the hold or the chalk bag, and how soft the edge is.
4. In Orion, while the proposal is live in the compare view: does the subject layer show on screen, or does the canvas look as if the mask covers nothing?
   That half was fixed from source and never watched.
5. Navigate away from the photo and back before approving.
   The matte PNG beside the raw must survive; before this session the sweep deleted it.
6. Open the gallery from the develop view.
   Report whether the grid lands on the photo you were editing or at the top of the folder.

## When carrying the edit across the shoot

7. Approve the hero frame, then `sync_edits` to three targets with default groups.
   Report whether each opens in Orion with a proposal waiting, the right white balance (not 5500 K), and the look you expect.
8. Repeat with `groups` including `masks`.
   Report the `mattes` entries per target: `coverage` per frame, and whether any target came back with an `error`.
   Check one target's climber with `get_stats` `region`.
9. A frame where the climber is small or half out of frame: does `detect_subject` still find them, and does the synced mask land on them?
10. With a radial or gradient in the hero's masks: say whether it landed in the right place on a target shot from the same spot, and whether it was wrong on one that was not.
11. Try `sync_edits` with `commit: true` on two targets only after you are sure.
    Report whether the open photo got a proposal instead of a sidecar, as documented.

## What to send back

- Every `error` string from any tool, verbatim.
- Any step above where the tool said one thing and the picture said another.
- Which of the numbers (coverage, luma before and after, token cost) you had to read to make a decision, and which you ignored.
- Anything you had to work out by trial that a tool description should have said.
