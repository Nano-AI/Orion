using Avalonia.Controls;
using Avalonia.Input;

namespace OrionShell;

/// One thing a person can ask for: a toolbar button, a menu item, a key.
/// `Owner` is the Swift file whose behaviour it mirrors. A null `Run` is a
/// stub: running it puts "not built yet" on the status line instead of
/// doing nothing. `Shortcut` is ", "-separated alternatives in Avalonia's
/// KeyGesture syntax ("Cmd+D0, D0"); Cmd is ⌘ on macOS and Ctrl elsewhere.
public sealed record Command(string Name, string Title, string? Shortcut, string Owner, Func<bool> CanRun, Action? Run) {
    public bool Built => Run != null;
    public IReadOnlyList<KeyGesture> Keys { get; } = Shortcut is null ? []
        : Shortcut.Split(", ").Select(k => KeyGesture.Parse(OperatingSystem.IsMacOS() ? k : k.Replace("Cmd", "Ctrl"))).ToArray();
}

/// The registry. Controls and keys reach commands by name only, so the
/// toolbar, the menus and the window's keys can never disagree about what a
/// command does or whether it can run. The rows are in CommandTable.cs.
public static class Commands {
    public static IReadOnlyList<Command> All { get; } = Index(CommandTable.All);

    /// Throws when two commands share a name or a key, naming both, so a merge
    /// that collides fails at startup instead of a key doing one of two things.
    public static IReadOnlyList<Command> Index(IEnumerable<Command> commands) {
        var list = commands.ToList();
        foreach (var clash in list.GroupBy(c => c.Name).Where(g => g.Count() > 1))
            throw new InvalidOperationException($"command '{clash.Key}' is registered {clash.Count()} times");
        foreach (var clash in list.SelectMany(c => c.Keys.Select(k => (Key: k, c.Name))).GroupBy(p => p.Key).Where(g => g.Count() > 1))
            throw new InvalidOperationException($"key {clash.Key} runs both {string.Join(" and ", clash.Select(p => p.Name))}");
        return list;
    }

    public static Command? Find(string name) => All.FirstOrDefault(c => c.Name == name);

    /// What greys a button out. False for a name nobody registered.
    public static bool CanRun(string name) => Find(name)?.CanRun() ?? false;

    /// Runs one by name and says what happened, which is what --command prints.
    public static string Run(string name) {
        if (Find(name) is not { } c) return $"no command '{name}'";
        if (!c.CanRun()) return $"{name}: not available now";
        if (c.Run is not { } run) {
            string said = $"not built yet: {name} ({c.Owner})";
            EngineModel.Shared.Say(said);
            return said;
        }
        run();
        return $"ran {name}";
    }

    /// The window's keys: the menu's shortcuts and handleKey's bare keys
    /// (OrionApp+Commands.swift) in one table. A key a control already took,
    /// or any key typed into a text box, is not a command; nor is one whose
    /// command cannot run now, so it reaches whoever else wants it.
    public static void Key(KeyEventArgs e) {
        if (e.Handled || e.Source is TextBox) return;
        if (All.FirstOrDefault(c => c.Keys.Any(k => k.Matches(e))) is not { } c || !c.CanRun()) return;
        Run(c.Name);
        e.Handled = true;
    }
}
