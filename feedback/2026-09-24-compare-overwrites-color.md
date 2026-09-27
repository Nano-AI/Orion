# Compare overwrote saved color edits in the installed build

The developer reported missing saturation/color edits in Seattle's
`DSC00088.ARW`, with a screenshot showing the edited half of Compare.

## Evidence

- The first sidecar read, before quitting the original installed app, contained
  five stars, crop `(0.06497755, 0.24199782, 0.7883159, 0.6178368)`, temperature
  5180 K and tint 0.05600017. Saturation, vibrance, grading and all mixer bands
  were zero. The file decoded and the saved crop rendered correctly.
- The replaced `/Applications` app predates #271's fix: it does not recognize
  the `workflowcheck` scenario verb. It was retained in temporary storage.
- Reproduced through that old app's actual GUI on a temporary sidecar beside a
  symlink to the RAW. Seeded saturation 0.8, blue hue -0.5 and blue saturation
  0.9, retaining the source crop. Clicking **View → Compare Original** zeroed
  saturation and every mixer band in the sidecar, preserving the crop.
- Repeated the same test with the newly packaged `/Applications/Orion.app`:
  saturation 0.8, blue hue -0.5, blue saturation 0.9 and crop all survived.
- `repro/desktop-interaction-reliability.txt`: 27 checks, zero failures.
  The nine required gates also passed this session (1104 engine, 4215 viewport).

## Cause and resolution

Already fixed in source by #271, but absent from the user's installed binary.
Compare renders a temporary neutral state with the edited crop. The old render
path notified autosave of that state; restoring the already-saved edit did not
cancel the queued neutral write. The display retained the edit while the disk
lost it. `Engine+Render.swift` now suppresses edit notifications during original
capture, and `Autosave.note` cancels obsolete writes when returning to saved state.
The reinstall for #272 delivers both fixes.

## Recovery limits

The original sidecar has not been changed during this investigation. It already
lacked the color values at the first read. No sibling saved version, second
matching file under Pictures, or local Time Machine snapshot was found. The
session log is replaced on launch and cannot recover the earlier session.
The screenshot can guide a new approximation but cannot supply exact parameters.
