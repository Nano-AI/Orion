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
    }

    init() {
        let defaults = UserDefaults.standard
        let savedWidth = defaults.double(forKey: Keys.width)
        width = savedWidth > 0 ? savedWidth : 360
        command = defaults.string(forKey: Keys.command) ?? "claude"

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
}
