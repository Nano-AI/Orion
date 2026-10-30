using System.Reflection;
using Avalonia.Controls;
using Avalonia.Media;

namespace OrionShell;

/// Where the shell puts a panel. Canvas panels stack, in order, over the photo.
/// Tools panels go under their Tab, or above the tab bar when Tab is null (the
/// histogram). Status sits under the tool column (OrionApp+Tools.swift footer).
/// Sheets are not placed in the window; they render alone for screenshots and
/// open from whatever command a slice gives them.
public enum Region { Toolbar, Tools, Status, Canvas, Filmstrip, Sheet }

/// Registers a Control with the shell. The type needs a public parameterless
/// constructor. `Name` is what `--panel` takes and what SLICES.md lists.
[AttributeUsage(AttributeTargets.Class)]
public sealed class PanelAttribute(string name, Region region, int order = 0) : Attribute {
    public string Name { get; } = name;
    public Region Region { get; } = region;
    public int Order { get; } = order;
    /// One of Panels.Tabs, for Region.Tools only.
    public string? Tab { get; init; }
}

public static class Panels {
    public sealed record Entry(string Name, Region Region, int Order, Type Type, string? Tab = null);

    /// ToolTab in OrionApp+Chrome.swift, in its order.
    public static readonly string[] Tabs = ["Light", "Color", "Detail", "Optics", "Mask", "Crop", "Presets"];
    /// Each tab's icon (#350): beside its name on the chosen tab, alone on the rest.
    public static readonly string[] TabIcons = ["sun.max", "circle.3", "magnifyingglass", "camera.aperture", "theatermasks.fill", "crop", "square.stack"];

    /// Every registered panel in this assembly, by region then order.
    public static IReadOnlyList<Entry> All { get; } = Index(
        typeof(Panels).Assembly.GetTypes()
            .Select(t => (t.GetCustomAttribute<PanelAttribute>(), t))
            .Where(p => p.Item1 != null)
            .Select(p => (p.Item1!, p.t)));

    /// Throws when two types claim one name, naming both, so a merge that
    /// collides fails at startup instead of hiding a panel.
    public static IReadOnlyList<Entry> Index(IEnumerable<(PanelAttribute Attr, Type Type)> panels) {
        var entries = panels.Select(p => new Entry(p.Attr.Name, p.Attr.Region, p.Attr.Order, p.Type, p.Attr.Tab)).ToList();
        foreach (var clash in entries.GroupBy(e => e.Name).Where(g => g.Count() > 1))
            throw new InvalidOperationException(
                $"panel '{clash.Key}' is registered by {string.Join(" and ", clash.Select(e => e.Type.FullName))}");
        foreach (var e in entries.Where(e => e.Tab != null && (e.Region != Region.Tools || !Tabs.Contains(e.Tab))))
            throw new InvalidOperationException(
                $"panel '{e.Name}' names tab '{e.Tab}': tabs are for Region.Tools and one of {string.Join(", ", Tabs)}");
        return entries.OrderBy(e => e.Region).ThenBy(e => e.Order).ThenBy(e => e.Name).ToList();
    }

    public static Entry? Find(string name) => All.FirstOrDefault(e => e.Name == name);

    /// The Tag on the placeholder Create returns when a constructor throws.
    public const string Failed = "panel-failed";

    /// The panel, or a red line saying why it could not be built, so one
    /// broken slice cannot take the others' screenshots down with it.
    public static Control Create(Type type) {
        try {
            return (Control)Activator.CreateInstance(type)!;
        } catch (Exception e) {
            var cause = e is TargetInvocationException { InnerException: { } inner } ? inner : e;
            Console.Error.WriteLine($"panel {type.Name} failed: {cause}");
            return new TextBlock {
                Tag = Failed,
                Text = $"{type.Name}: {cause.GetType().Name}: {cause.Message}",
                Foreground = Palette.Warn, TextWrapping = TextWrapping.Wrap, Margin = new(Space.Pad),
            };
        }
    }
}
