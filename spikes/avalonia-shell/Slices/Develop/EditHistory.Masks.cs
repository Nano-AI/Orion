using OrionShell.Masks;

namespace OrionShell.Develop;

/// The mask rows in each history entry, as Swift's entries hold
/// MaskComponentState: every number and dab the engine has, plus the name,
/// matte id and matte source it never holds (#207). Put back by position
/// after an undo, the ids slid onto the wrong rows when a row came or went,
/// and a matte's raster followed the wrong id; carried in the entry, each
/// row comes back with its own.
static partial class EditHistory {
    /// The engine's rows, named from the group, which holds names and ids by position at every settle.
    static MaskShape[] MaskRows() {
        var rows = E.Session?.ReadMaskGroup() ?? [];
        var named = MaskGroup.Shared.Shapes;
        for (int i = 0; i < rows.Count && i < named.Count; i++)
            (rows[i].Name, rows[i].MatteId, rows[i].MatteSource) = (named[i].Name, named[i].MatteId, named[i].MatteSource);
        return [.. rows];
    }

    static bool SameMasks(MaskShape[] a, MaskShape[] b) =>
        a.Length == b.Length && a.Zip(b).All(p =>
            Session.MaskFields.All(f => f.Value(p.First) == f.Value(p.Second))
            && p.First.BrushStroke.SequenceEqual(p.Second.BrushStroke) && p.First.BrushErase.SequenceEqual(p.Second.BrushErase)
            && p.First.Name == p.Second.Name && p.First.MatteId == p.Second.MatteId && p.First.MatteSource == p.Second.MatteSource);

    /// Fresh copies of an entry's rows into the group, before the render, so the
    /// autosave that render triggers and the panel's reread both find the entry's
    /// names and ids. The entry's own rows stay untouched for the next undo.
    static List<MaskShape> RestoreMaskNames(MaskShape[] entry) {
        var rows = entry.Select(Copy).ToList();
        var shapes = MaskGroup.Shared.Shapes;
        shapes.Clear();
        shapes.AddRange(rows);
        return rows;
    }

    static MaskShape Copy(MaskShape m) {
        var c = new MaskShape { Name = m.Name, MatteId = m.MatteId, MatteSource = m.MatteSource };
        foreach (var (_, value, put) in Session.MaskFields) put(c, value(m));
        c.BrushStroke.AddRange(m.BrushStroke);
        c.BrushErase.AddRange(m.BrushErase);
        return c;
    }
}
