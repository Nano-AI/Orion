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

    /// Resolved once at launch (below) rather than on every SwiftUI body
    /// evaluation: there is no in-app control that changes it, and
    /// recomputing it from a view's `body` would mean writing `notice`, an
    /// observed property, on every render.
    let workingDirectory: URL

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
        if let saved = defaults.string(forKey: Keys.workingDirectory) {
            workingDirectory = URL(fileURLWithPath: saved)
        } else {
            let repo = Bundle.main.bundleURL
                .deletingLastPathComponent() // build/
                .deletingLastPathComponent() // repo root
            if fm.fileExists(atPath: repo.appendingPathComponent(".mcp.json").path) {
                workingDirectory = repo
            } else {
                workingDirectory = fm.homeDirectoryForCurrentUser
                notice = "no .mcp.json here; the Orion tools will not load"
            }
        }
    }

    func toggle() { isOpen.toggle() }
    func restart() { restartToken = UUID() }

    func increaseFontSize() { fontSize = min(fontSize + 1, Self.maxFontSize) }
    func decreaseFontSize() { fontSize = max(fontSize - 1, Self.minFontSize) }
    func resetFontSize() { fontSize = Self.defaultFontSize }
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
