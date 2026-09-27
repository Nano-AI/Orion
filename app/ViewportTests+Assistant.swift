// The assistant terminal's pure logic, checked without a window: which of
// Orion's two key handlers a keyDown event should reach (the actual bug —
// the terminal's own keystrokes were firing Orion's rating/reject/crop
// shortcuts instead of reaching the shell), ⌘=/⌘-/⌘0 recognition for its
// font, and the font-size clamp. See OrionApp+Commands.swift's key monitor
// for the one real caller, which has the NSEvent/NSWindow this cannot.

import Foundation

extension ViewportTests {

    static func testAssistantKeyRouteFollowsFirstResponder() {
        report(AssistantKeyRoute.destination(terminalIsFirstResponder: true) == .terminal,
               "routes to the terminal when it is first responder")
        report(AssistantKeyRoute.destination(terminalIsFirstResponder: false) == .app,
               "routes to Orion's own shortcuts otherwise")
    }

    static func testAssistantFontShortcutMatchesCommandEqualsMinusAndZero() {
        report(AssistantFontShortcut.match(characters: "=", command: true) == .increase,
               "⌘= increases the font")
        report(AssistantFontShortcut.match(characters: "+", command: true) == .increase,
               "⌘+ (shifted ⌘=) also increases")
        report(AssistantFontShortcut.match(characters: "-", command: true) == .decrease,
               "⌘- decreases the font")
        report(AssistantFontShortcut.match(characters: "0", command: true) == .reset,
               "⌘0 resets the font")
    }

    /// The other half of the fix: everything that must NOT be treated as a
    /// font shortcut, so it reaches the shell (a bare key) or keeps doing
    /// whatever it already did (⌘ with anything else, like ⌘C).
    static func testAssistantFontShortcutIgnoresEverythingElse() {
        report(AssistantFontShortcut.match(characters: "=", command: false) == nil,
               "bare = is not a font shortcut — it must reach the shell untouched")
        report(AssistantFontShortcut.match(characters: "c", command: true) == nil,
               "⌘C is not a font shortcut")
        report(AssistantFontShortcut.match(characters: "9", command: true) == nil,
               "⌘9 is not a font shortcut — it keeps doing whatever it did before")
        report(AssistantFontShortcut.match(characters: nil, command: true) == nil,
               "no characters is not a font shortcut")
    }

    static func testAssistantFontSizeClampsAtBothEnds() {
        let model = AssistantPanelModel()
        model.resetFontSize()
        report(model.fontSize == AssistantPanelModel.defaultFontSize,
               "resets to the default", "\(model.fontSize)")

        for _ in 0..<40 { model.increaseFontSize() }
        report(model.fontSize == AssistantPanelModel.maxFontSize,
               "⌘= stops at the max rather than growing without bound", "\(model.fontSize)")

        for _ in 0..<80 { model.decreaseFontSize() }
        report(model.fontSize == AssistantPanelModel.minFontSize,
               "⌘- stops at the min rather than shrinking without bound", "\(model.fontSize)")
    }

    static func testAssistantArgvBuilderAppendsClaude() {
        let argvClaude = AssistantProcess.buildArgv(for: "claude")
        let hasAppendSystemPrompt = argvClaude.contains("--append-system-prompt")
        let orionContextIndex = argvClaude.firstIndex { $0.contains("Orion") && $0.contains("current_photo") }
        report(hasAppendSystemPrompt,
               "claude argv contains --append-system-prompt")
        report(orionContextIndex != nil,
               "claude argv contains the Orion context after --append-system-prompt")
    }

    static func testAssistantArgvBuilderOmitsCodex() {
        let argvCodex = AssistantProcess.buildArgv(for: "codex")
        let hasAppendSystemPrompt = argvCodex.contains("--append-system-prompt")
        report(!hasAppendSystemPrompt,
                "codex argv does not contain --append-system-prompt")
    }

    /// Reproduce Finder's minimal PATH with commands installed only by .zshrc.
    /// Execute the product argv so a regression to login-only actually fails.
    static func testAssistantLaunchLoadsInteractivePath() {
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("orion assistant ' \(UUID())")
        do {
            try fm.createDirectory(at: root, withIntermediateDirectories: true)
            defer { try? fm.removeItem(at: root) }
            try "export PATH=\"$ZDOTDIR:$PATH\"\n".write(
                to: root.appendingPathComponent(".zshrc"), atomically: true, encoding: .utf8)
            for command in AssistantPanelModel.commands {
                let stub = root.appendingPathComponent(command)
                try "#!/bin/sh\nprintf '%s\\n' \"$0\" \"$@\"\n".write(
                    to: stub, atomically: true, encoding: .utf8)
                try fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: stub.path)
                let process = Process()
                let output = Pipe()
                process.executableURL = URL(fileURLWithPath: "/bin/zsh")
                process.arguments = AssistantProcess.buildArgv(for: command)
                process.environment = ["HOME": root.path, "ZDOTDIR": root.path,
                                       "PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "TERM": "xterm-256color"]
                process.standardOutput = output
                process.standardError = FileHandle.nullDevice
                try process.run()
                let text = String(decoding: output.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
                process.waitUntilExit()
                report(process.terminationStatus == 0 && text.hasPrefix(stub.path + "\n"),
                       "\(command) launches using .zshrc PATH from a GUI environment")
                if command == "claude" {
                    report(text.contains("--append-system-prompt\n" + AssistantProcess.orionContext + "\n"),
                           "Claude receives the complete context as one literal argument")
                }
            }
            report(AssistantPanelModel.configurationNotice(in: root) != nil,
                   "missing MCP configuration reports how to choose a tools folder")
            try "{}".write(to: root.appendingPathComponent(".mcp.json"), atomically: true, encoding: .utf8)
            report(AssistantPanelModel.configurationNotice(in: root) == nil,
                   "configured tools folder clears the notice")
        } catch {
            report(false, "assistant launch fixture", "\(error)")
        }
    }
}
