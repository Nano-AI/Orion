import SwiftUI

// The Presets tab: saved looks, copy, paste, sync and batch export — and the
// versions of this one photograph, which share the tab rather than taking an
// eighth plate in the bar. See the note on `versionsPanel` for why.

/// The group toggles for saving a preset. A row of small switches rather than a
/// menu: the set is short, it is the thing most worth seeing before pressing
/// Save, and a menu would hide it behind a click.
private struct FlowGroups: View {
    @Binding var selection: Set<PresetGroup>

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            ForEach(PresetGroup.allCases) { g in
                Toggle(isOn: Binding(
                    get: { selection.contains(g) },
                    set: { on in
                        if on { selection.insert(g) } else { selection.remove(g) }
                    })) {
                    HStack(spacing: 4) {
                        Text(g.title).font(.system(size: 10))
                        Text(g.covers)
                            .font(.system(size: 9))
                            .foregroundStyle(Palette.faint)
                            .lineLimit(1)
                    }
                }
                .toggleStyle(.checkbox)
                .controlSize(.mini)
            }
        }
    }
}

extension Editor {

    /// Presets, on a tab of their own.
    ///
    /// ⚠ It lived at the top of Light, where it was a hundred and twenty lines
    /// in front of the Exposure slider — the single most-reached-for control in
    /// the program. A section that has to be scrolled *past* on every
    /// photograph is in the wrong place however good it is, and Light had five
    /// sections of which this was by far the largest.
    ///
    /// Last in the bar rather than first: the tab order runs most-used to
    /// least, and a preset is applied occasionally while Light is touched on
    /// every frame.
    var presetsPanel: some View {
        Group {
            versionsPanel

            section("Presets") {
                if presets.presets.isEmpty {
                    Text("No presets yet.")
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                }
                VStack(spacing: 2) {
                    ForEach(presets.presets) { p in
                        HStack(spacing: 6) {
                            Button { engine.apply(preset: p) } label: {
                                HStack(spacing: 6) {
                                    Text(p.name)
                                    Spacer(minLength: 0)
                                    // What it will disturb, before it is
                                    // pressed — a look that silently reset the
                                    // sharpening would be a nasty surprise.
                                    Text(p.groups.count == PresetGroup.allCases.count
                                         ? "all" : "\(p.groups.count) groups")
                                        .foregroundStyle(Palette.faint)
                                }
                                .font(.system(size: 11))
                                .padding(.horizontal, 6)
                                .padding(.vertical, 3)
                                .frame(maxWidth: .infinity)
                                .background(Palette.raised)
                                .clipShape(RoundedRectangle(cornerRadius: 3))
                                .contentShape(RoundedRectangle(cornerRadius: 3))
                            }
                            .buttonStyle(.plain)

                            if !p.builtIn {
                                Button {
                                    presets.remove(p)
                                } label: {
                                    Image(systemName: "minus.circle")
                                        .font(.system(size: 10))
                                        .foregroundStyle(Palette.faint)
                                }
                                .buttonStyle(.plain)
                            }
                        }
                    }
                }

                HStack(spacing: 6) {
                    TextField("New preset", text: $presetName)
                        .textFieldStyle(.roundedBorder)
                        .font(.system(size: 11))
                    Button("Save") {
                        // ⚠ A preset that did not reach the file is in the list
                        // and gone at the next launch. The name is kept in the
                        // field on a failure so the gesture can be retried.
                        if presets.add(name: presetName,
                                       groups: presetGroups,
                                       state: engine.state) {
                            presetName = ""
                        } else {
                            message = presets.lastFailure
                                   ?? "That preset could not be saved."
                        }
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                    .disabled(presetName.trimmingCharacters(in: .whitespaces).isEmpty
                              || presetGroups.isEmpty)
                }

                // Which groups a *saved* preset will carry. Shown rather than
                // assumed, because "save a preset" means different things to
                // different people and the difference is exactly this list.
                FlowGroups(selection: $presetGroups)

                // Copy, paste and sync share the group checkboxes above: a
                // paste *is* a preset that was never named, so offering it a
                // second, separate list of groups would be two answers to one
                // question.
                HStack(spacing: 6) {
                    Button("Copy") { copied = engine.state }
                        .disabled(!engine.isLoaded)
                    Button("Paste") {
                        guard let copied else { return }
                        engine.apply(preset: Preset(name: "Paste",
                                                    groups: presetGroups,
                                                    state: copied))
                    }
                    .disabled(copied == nil || presetGroups.isEmpty)
                    Button("Sync all…") { confirmingSync = true }
                        .disabled(copied == nil || presetGroups.isEmpty
                                  || library.photos.count < 2)
                    Spacer(minLength: 0)
                }
                .buttonStyle(.bordered)
                .controlSize(.small)

                if copied != nil {
                    Text("Settings copied. Paste puts them on this photo; "
                       + "Sync writes them to every photo in view.")
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                        .fixedSize(horizontal: false, vertical: true)
                }

                // Batch export lives beside sync because they are the same
                // gesture from the photographer's side — do this to all of
                // them — and differ only in whether the result is a sidecar or
                // a file.
                HStack(spacing: 6) {
                    if let p = batchProgress {
                        Text("Exporting \(p.done) of \(p.total)…")
                            .font(.system(size: 10))
                            .foregroundStyle(Palette.dim)
                        Button("Stop") { batchCancelled = true }
                            .buttonStyle(.bordered)
                            .controlSize(.small)
                    } else {
                        // ⚠ Guarded on `exportTargets`, not on `photos`. A
                        // folder of nothing but rejects has photographs in it
                        // and nothing this button would write, and an enabled
                        // control there opens a folder panel onto an empty
                        // batch.
                        Button("Export all…") { runBatchExport() }
                            .buttonStyle(.bordered)
                            .controlSize(.small)
                            .disabled(!engine.isLoaded || library.exportTargets.isEmpty)
                    }
                    Spacer(minLength: 0)
                }

                // What the batch will actually write, before it is pressed. The
                // folder panel says this too, but that is the last moment and
                // this is the one where the count on the button gets explained.
                if batchProgress == nil, library.rejectedInView > 0 {
                    Text("Exports \(library.exportTargets.count) photos. "
                       + (library.rejectedInView == 1
                            ? "1 rejected photo is left out."
                            : "\(library.rejectedInView) rejected photos are left out."))
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                        .fixedSize(horizontal: false, vertical: true)
                }

                Text("A preset changes only the groups it carries and leaves "
                   + "everything else alone. The crop, the dust spots and the "
                   + "masks are never included — those belong to one photograph.")
                    .font(.system(size: 10))
                    .foregroundStyle(Palette.faint)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    /// Versions — this photograph's saved edits. See `Snapshots.swift`.
    ///
    /// ⚠ **First in the Presets tab rather than a tab of its own, and that is a
    /// measurement rather than a preference.** A version wants its own tab: it
    /// belongs to exactly one photograph where a preset belongs to none, and
    /// sharing a tab invites the reading that a version is a look you could drop
    /// on the next frame — it is the opposite, since a version carries the crop,
    /// the dust and the masks a preset refuses to. But the tab bar divides 364
    /// points between its plates, and an eighth leaves 42 points for a word that
    /// needs 51. Rendered with the eighth tab in place, the bar read
    /// **PRESE… VERSI…** — the new tab cost the old one its name too. See the
    /// note on `ToolTab`.
    ///
    /// First rather than last, because the tab's other four sections are all
    /// *between* photographs — a look, a paste, a sync, a batch — and this is
    /// the only one about the photograph in front of you.
    ///
    /// ⚠ **What a version cannot promise is said before it is pressed, not
    /// after.** A raster mask is a file beside the photograph; Orion's sweep is
    /// pinned against taking one a version names, but a photograph copied
    /// without its siblings arrives with the PNGs gone. Restoring then gives a
    /// mask row that covers nothing, which changes the picture with nothing on
    /// screen saying why. The row carries the count and the button says it.
    @ViewBuilder
    var versionsPanel: some View {
        Group {
            section("Versions") {
                if snapshots.unreadable {
                    Text(SnapshotStore.Refusal
                            .fileUnreadable(snapshots.photo.map {
                                SnapshotStore.url(photo: $0).lastPathComponent
                            } ?? "The versions file").errorDescription ?? "")
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.star)
                        .fixedSize(horizontal: false, vertical: true)
                } else if snapshots.snapshots.isEmpty {
                    Text(engine.isLoaded
                         ? "No versions of this photo yet."
                         : "Open a photo to save a version of it.")
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                }

                VStack(spacing: 2) {
                    ForEach(snapshots.snapshots) { s in
                        versionRow(s)
                    }
                }

                HStack(spacing: 6) {
                    TextField("New version", text: $snapshotName)
                        .textFieldStyle(.roundedBorder)
                        .font(.system(size: 11))
                    Button("Save") { saveVersion() }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                        .disabled(!engine.isLoaded || snapshots.unreadable
                                  || snapshotName.trimmingCharacters(in: .whitespaces).isEmpty)
                }

                Text("A version is this photo's whole edit under a name — the "
                   + "crop, the dust and the masks included. Restoring one "
                   + "keeps what you had first, as “Before restoring…”, and "
                   + "⌘Z takes it back in one step.")
                    .font(.system(size: 10))
                    .foregroundStyle(Palette.faint)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    /// One saved version: restore, rename in place, delete.
    @ViewBuilder
    private func versionRow(_ s: Snapshot) -> some View {
        // ⚠ The *store's* photograph, not the editor's `current`. They are the
        // same file in the app, and only one of them is the file these mattes
        // sit beside — a caption derived from the other would be right by
        // coincidence, and silently absent anywhere the two are not both set.
        let missing = snapshots.photo.map { SnapshotStore.missingMattes(s, photo: $0) } ?? []

        HStack(spacing: 6) {
            if renamingSnapshot == s.id {
                TextField("Name", text: $renameText)
                    .textFieldStyle(.roundedBorder)
                    .font(.system(size: 11))
                    .onSubmit { commitRename(s) }
                Button("Done") { commitRename(s) }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
            } else {
                Button { restoreVersion(s) } label: {
                    VStack(alignment: .leading, spacing: 1) {
                        HStack(spacing: 6) {
                            Text(s.name).lineLimit(1).truncationMode(.middle)
                            Spacer(minLength: 0)
                            Text(Self.stamp.string(from: s.created))
                                .foregroundStyle(Palette.faint)
                        }
                        .font(.system(size: 11))

                        if !missing.isEmpty {
                            // ⚠ Named, and in the color the app uses for
                            // "look at this". A version that quietly restores
                            // an empty mask is the defect this line exists to
                            // refuse.
                            Text("\(missing.count) selection\(missing.count == 1 ? "" : "s") "
                               + "missing — \(missing.joined(separator: ", "))")
                                .font(.system(size: 9))
                                .foregroundStyle(Palette.star)
                        } else if s.automatic {
                            Text("kept automatically · rename to keep it")
                                .font(.system(size: 9))
                                .foregroundStyle(Palette.faint)
                        }
                    }
                    .padding(.horizontal, 6)
                    .padding(.vertical, 3)
                    .frame(maxWidth: .infinity)
                    .background(Palette.raised)
                    .clipShape(RoundedRectangle(cornerRadius: 3))
                    .contentShape(RoundedRectangle(cornerRadius: 3))
                }
                .buttonStyle(.plain)
                .disabled(!engine.isLoaded)

                Button {
                    renamingSnapshot = s.id
                    renameText = s.name
                } label: {
                    Image(systemName: "pencil")
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                }
                .buttonStyle(.plain)

                Button {
                    do { try snapshots.remove(s.id) }
                    catch { message = error.localizedDescription }
                } label: {
                    Image(systemName: "minus.circle")
                        .font(.system(size: 10))
                        .foregroundStyle(Palette.faint)
                }
                .buttonStyle(.plain)
            }
        }
    }

    /// Short and unambiguous. A version list is read by date more often than by
    /// name — "the one from before lunch" — so the stamp is not decoration.
    private static let stamp: DateFormatter = {
        let f = DateFormatter()
        f.dateStyle = .short
        f.timeStyle = .short
        return f
    }()

    private func saveVersion() {
        do {
            try snapshots.save(name: snapshotName, state: engine.state)
            snapshotName = ""
        } catch {
            message = error.localizedDescription
        }
    }

    private func commitRename(_ s: Snapshot) {
        do { try snapshots.rename(s.id, to: renameText) }
        catch { message = error.localizedDescription }
        renamingSnapshot = nil
    }

    /// ⚠ Through `SnapshotStore.restore`, which keeps the working edit before
    /// it is overwritten. The two steps are one call for exactly the reason the
    /// matte sweep is one function: an order written out at each call site is
    /// an order one of them stops following.
    private func restoreVersion(_ s: Snapshot) {
        snapshots.restore(s, working: engine.state) { snapshot in
            engine.restore(snapshot: snapshot, photo: current)
        }
    }
}
