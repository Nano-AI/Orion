// The assistant column's state: which command runs, where, how wide the
// column is, and the one-line notice when the working directory looks wrong.
//
// Plain @Observable, zero SwiftUI types — see CLAUDE.md's UI rule — so this
// could be re-hosted in AppKit without dragging SwiftUI along. AppKit-facing
// process lifecycle (launching SwiftTerm, killing it on close and on quit)
// lives in AssistantPanel.swift instead, next to the NSViewRepresentable that
// needs it.

import Foundation

@Observable
final class AssistantPanelModel {
    static let commands = ["claude", "codex"]

    var isOpen = false

    var width: Double {
        didSet { UserDefaults.standard.set(width, forKey: Keys.width) }
    }

    var command: String {
        didSet { UserDefaults.standard.set(command, forKey: Keys.command) }
    }

    /// Bumped by `restart()`; `AssistantTerminalView` relaunches whenever this
    /// changes, which is what gives the header's restart button something to
    /// compare against even when the command and directory did not change.
    private(set) var restartToken = UUID()

    /// ⌘= / ⌘- / ⌘0 while the terminal is focused (Terminal.app's own
    /// convention) — see `handleTerminalFontShortcut` in
    /// OrionApp+Commands.swift, the one place that already sees every key
    /// event ahead of the rest of the app.
    var fontSize: Double {
        didSet { UserDefaults.standard.set(fontSize, forKey: Keys.fontSize) }
    }
    static let minFontSize: Double = 8
    static let maxFontSize: Double = 28
    static let defaultFontSize: Double = 13

    /// Resolved at launch or by the tools-folder picker, never during rendering.
    private(set) var workingDirectory: URL

    /// Set while resolving `workingDirectory`, and shown as a one-line notice
    /// in the column header rather than silently starting the shell
    /// somewhere the photographer did not expect.
    private(set) var notice: String?

    private enum Keys {
        static let width = "assistantPanelWidth"
        static let command = "assistantCommand"
        static let workingDirectory = "assistantWorkingDirectory"
        static let fontSize = "assistantFontSize"
    }

    init() {
        let defaults = UserDefaults.standard
        let savedWidth = defaults.double(forKey: Keys.width)
        width = savedWidth > 0 ? savedWidth : 360
        command = defaults.string(forKey: Keys.command) ?? "claude"
        let savedFontSize = defaults.double(forKey: Keys.fontSize)
        fontSize = savedFontSize > 0 ? savedFontSize : Self.defaultFontSize

        // UserDefaults `assistantWorkingDirectory` when set; otherwise the
        // Orion repo, derived from the app bundle two levels up
        // (build/Orion.app -> repo) and verified by the presence of
        // `.mcp.json` — that file is what tells the launched agent which MCP
        // server to load. Falls back to the user's home, with `notice` set,
        // when neither checks out.
        let fm = FileManager.default
        if let saved = defaults.string(forKey: Keys.workingDirectory),
           (try? URL(fileURLWithPath: saved).resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true {
            workingDirectory = URL(fileURLWithPath: saved)
        } else {
            let repo = Bundle.main.bundleURL
                .deletingLastPathComponent() // build/
                .deletingLastPathComponent() // repo root
            if fm.fileExists(atPath: repo.appendingPathComponent(".mcp.json").path) {
                workingDirectory = repo
            } else {
                workingDirectory = fm.homeDirectoryForCurrentUser
            }
        }
        refreshQuarantine()
    }

    /// The `orion-mcp` a packaged app carries beside its own executable
    /// (tools/package-app.sh, #310). nil in a development build, which has
    /// the repo's `.mcp.json` instead.
    static let bundledServer: URL? = {
        guard let dir = Bundle.main.executableURL?.deletingLastPathComponent() else { return nil }
        let server = dir.appendingPathComponent("orion-mcp")
        return FileManager.default.isExecutableFile(atPath: server.path) ? server : nil
    }()

    /// The server to hand the agent on its command line: the bundled one,
    /// unless the working directory registers its own through `.mcp.json`,
    /// where passing both would load two servers named `orion`.
    static func launchServer(in directory: URL, bundled: URL? = bundledServer) -> URL? {
        hasMCPConfig(directory) ? nil : bundled
    }

    var launchServer: URL? { Self.launchServer(in: workingDirectory) }

    static func configurationNotice(in directory: URL, bundled: URL? = bundledServer,
                                    quarantined: Bool = false) -> String? {
        if hasMCPConfig(directory) { return nil }
        if bundled == nil { return "Choose a tools folder containing .mcp.json" }
        return quarantined ? "macOS is blocking Orion’s tools" : nil
    }

    /// The bundled server still carries the download's quarantine flag, so
    /// Gatekeeper will refuse to run it for the agent (#310). Shown with an
    /// Allow button rather than cleared at launch: clearing it is the
    /// photographer's decision, the same one as README-FIRST's `xattr` line.
    private(set) var toolsQuarantined = false

    /// What the Allow button does: `xattr -dr com.apple.quarantine` on this
    /// app. Fails, and says so, where the bundle is read-only - a disk image
    /// opened in place.
    func allowTools() {
        if !Quarantine.clear(Bundle.main.bundleURL) {
            notice = "Move Orion to Applications, then Allow"
            return
        }
        refreshQuarantine()
        restart()
    }

    private func refreshQuarantine() {
        toolsQuarantined = Self.bundledServer.map { Quarantine.isSet($0) } ?? false
        notice = Self.configurationNotice(in: workingDirectory, quarantined: toolsQuarantined)
    }

    private static func hasMCPConfig(_ directory: URL) -> Bool {
        FileManager.default.fileExists(atPath: directory.appendingPathComponent(".mcp.json").path)
    }

    func setWorkingDirectory(_ directory: URL) {
        workingDirectory = directory
        UserDefaults.standard.set(directory.path, forKey: Keys.workingDirectory)
        refreshQuarantine()
        restart()
    }

    func toggle() { isOpen.toggle() }
    func restart() { restartToken = UUID() }

    func increaseFontSize() { fontSize = min(fontSize + 1, Self.maxFontSize) }
    func decreaseFontSize() { fontSize = max(fontSize - 1, Self.minFontSize) }
    func resetFontSize() { fontSize = Self.defaultFontSize }
}

enum AssistantProcess {
    static let orionContext = "Orion is a RAW photo editor and this server is its agent surface. The photographer is looking at one photo in Orion; when they say 'this image', 'this photo', 'the current one' or give no path, that photo is the one `current_photo` returns, and every tool's `path` defaults to it, so never ask which file. Start any edit with `describe_edits` and `get_stats`; `get_edits` reads the current or proposed settings without writing anything. All values are absolute, never deltas. Local adjustments run three to four times a global's at the top of the scale: a blown sky near luma 0.98 barely moves under a small masked exposure pull, so probe strong and ease off. Numbers before pixels: `get_stats` with `region` reads a patch (luma, clippedHigh, shading, noise) for a fraction of a proxy's cost, and `detect_faces` places a radial mask on a portrait. Judge sharpening, skin and mask edges with `get_proxy`'s `region`, never a whole 2048 px frame. `detect_subject` lifts the subject, a person or the sky into a matte for a kind-4 mask row, and `sync_edits` carries one approved photo's look onto the rest of a shoot as proposals. Read `capture.iso` from `get_stats` or `current_photo` before deciding how hard to denoise. Proposals appear live in Orion's compare view; the photographer approves or rejects there, so after `propose_edit` say what you changed and stop; do not call `approve_edit` unless asked."

    /// Interactive + login matches Terminal.app: CLI installs commonly add PATH
    /// in .zshrc, which a login-only shell skips when launched from Finder.
    /// Pass every command and argument as data, never shell source.
    ///
    /// `server`, when given, registers Orion's MCP server for this session
    /// only, so a downloaded app's assistant has its tools without the
    /// photographer writing any configuration: Claude Code takes the JSON
    /// itself, Codex a TOML-valued `-c` override of its config.
    static func buildArgv(for command: String, server: URL? = nil) -> [String] {
        let shell = ["-l", "-i", "-c", "exec \"$0\" \"$@\"", command]
        switch command {
        case "claude":
            return shell + (server.map { ["--mcp-config", claudeConfig($0)] } ?? [])
                + ["--append-system-prompt", orionContext]
        case "codex":
            return shell + (server.map { ["-c", "mcp_servers.orion.command=" + tomlString($0.path)] } ?? [])
        default:
            return shell
        }
    }

    /// `claude mcp add`, single-quoted for a shell, for pasting into a
    /// terminal outside Orion - after clearing the download's quarantine
    /// from the app, without which Gatekeeper refuses the server (#310).
    static func claudeAddCommand(_ server: URL, app: URL) -> String {
        "xattr -dr com.apple.quarantine " + shellQuoted(app.path)
            + " && claude mcp add orion -- " + shellQuoted(server.path)
    }

    private static func shellQuoted(_ text: String) -> String {
        "'" + text.replacingOccurrences(of: "'", with: "'\\''") + "'"
    }

    static func claudeConfig(_ server: URL) -> String {
        let config = ["mcpServers": ["orion": ["command": server.path]]]
        let data = (try? JSONSerialization.data(withJSONObject: config, options: [.sortedKeys, .withoutEscapingSlashes])) ?? Data()
        return String(decoding: data, as: UTF8.self)
    }

    private static func tomlString(_ text: String) -> String {
        "\"" + text.replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"") + "\""
    }
}

/// The download's quarantine flag. Orion is signed ad hoc and not notarized,
/// so Gatekeeper refuses a quarantined executable launched from a terminal:
/// measured on alpha.6, `spctl` rejects `orion-mcp` and it never answers
/// `initialize`. Approving Orion.app in Finder is about the app, not about a
/// helper an agent spawns.
enum Quarantine {
    static let attribute = "com.apple.quarantine"

    static func isSet(_ url: URL, attribute: String = attribute) -> Bool {
        getxattr(url.path, attribute, nil, 0, 0, XATTR_NOFOLLOW) >= 0
    }

    /// From `root` and everything under it, as `xattr -dr` does. False if any
    /// file that carries it could not be cleared.
    static func clear(_ root: URL, attribute: String = attribute) -> Bool {
        let inside = FileManager.default.enumerator(at: root, includingPropertiesForKeys: nil)?
            .compactMap { $0 as? URL } ?? []
        var cleared = true
        for url in [root] + inside where removexattr(url.path, attribute, XATTR_NOFOLLOW) != 0 && errno != ENOATTR {
            cleared = false
        }
        return cleared
    }
}

/// Which of Orion's two key handlers a keyDown event should reach — the
/// assistant terminal (every bare key, plus ⌘=/⌘-/⌘0 for its font) or
/// Orion's own shortcuts. Pure: takes only whether the terminal is first
/// responder, not a real `NSWindow`/`NSResponder`, so the routing decision
/// itself — the fix for the terminal's keystrokes firing Orion's shortcuts
/// instead of reaching the shell — is unit-tested without building a window.
/// The one real caller is `installKeyMonitor` in OrionApp+Commands.swift,
/// which has the actual `NSEvent`/`NSWindow`; see ViewportTests+Assistant.swift
/// for the test.
enum AssistantKeyRoute: Equatable {
    case terminal
    case app

    static func destination(terminalIsFirstResponder: Bool) -> AssistantKeyRoute {
        terminalIsFirstResponder ? .terminal : .app
    }
}

/// ⌘=/⌘-/⌘0 while the terminal is focused (Terminal.app's own convention).
/// `characters` is `NSEvent.charactersIgnoringModifiers`; anything else,
/// including every other ⌘-combination and every bare key, returns `nil` —
/// "not a font shortcut, let it through unmodified."
enum AssistantFontShortcut: Equatable {
    case increase, decrease, reset

    static func match(characters: String?, command: Bool) -> AssistantFontShortcut? {
        guard command else { return nil }
        switch characters {
        case "=", "+": return .increase
        case "-": return .decrease
        case "0": return .reset
        default: return nil
        }
    }
}
