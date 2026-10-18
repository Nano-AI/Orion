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

    /// A downloaded app has no `.mcp.json` anywhere, only the `orion-mcp`
    /// beside its executable (#310): both agents must be handed it on the
    /// command line, Claude before its system prompt so the variadic
    /// `--mcp-config` stops at a flag.
    static func testAssistantArgvRegistersBundledServer() {
        let server = URL(fileURLWithPath: "/Applications/Orion's \"test\".app/Contents/MacOS/orion-mcp")
        let claude = AssistantProcess.buildArgv(for: "claude", server: server)
        if let i = claude.firstIndex(of: "--mcp-config"), i + 1 < claude.count,
           let data = claude[i + 1].data(using: .utf8),
           let json = try? JSONSerialization.jsonObject(with: data) as? [String: [String: [String: String]]] {
            report(json["mcpServers"]?["orion"]?["command"] == server.path,
                   "claude --mcp-config names the bundled server by its exact path", claude[i + 1])
            report(claude.firstIndex(of: "--append-system-prompt").map { $0 > i } == true,
                   "claude --mcp-config comes before --append-system-prompt")
        } else {
            report(false, "claude argv carries --mcp-config with parseable JSON", "\(claude)")
        }
        let codex = AssistantProcess.buildArgv(for: "codex", server: server)
        report(codex.suffix(2) == ["-c", "mcp_servers.orion.command=\"/Applications/Orion's \\\"test\\\".app/Contents/MacOS/orion-mcp\""],
               "codex -c registers the bundled server as a TOML string", "\(codex.suffix(2))")
        report(!AssistantProcess.buildArgv(for: "claude").contains("--mcp-config")
               && !AssistantProcess.buildArgv(for: "codex").contains { $0.hasPrefix("mcp_servers.") },
               "no server, no MCP flags")
        let app = URL(fileURLWithPath: "/Applications/Orion's \"test\".app")
        let add = AssistantProcess.claudeAddCommand(server, app: app)
        report(add == "xattr -dr com.apple.quarantine '/Applications/Orion'\\''s \"test\".app' && "
               + "claude mcp add orion -- '/Applications/Orion'\\''s \"test\".app/Contents/MacOS/orion-mcp'",
               "the copied setup line clears quarantine, then adds the server, both quoted for a shell", add)
    }

    /// The Allow button's mechanism, on a private attribute name: the real
    /// quarantine flag is one a sandboxed test runner may not remove, and the
    /// code path is the same for any name.
    static func testQuarantineClearsEveryFileAndReports() {
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("orion quarantine \(UUID()).app")
        let name = "dev.orion.test-quarantine"
        do {
            let macOS = root.appendingPathComponent("Contents/MacOS")
            try fm.createDirectory(at: macOS, withIntermediateDirectories: true)
            defer { try? fm.removeItem(at: root) }
            let server = macOS.appendingPathComponent("orion-mcp")
            try Data().write(to: server)
            for url in [root, macOS, server] {
                _ = setxattr(url.path, name, "1", 1, 0, XATTR_NOFOLLOW)
            }
            report(Quarantine.isSet(server, attribute: name), "a flagged helper reads as quarantined")
            report(Quarantine.clear(root, attribute: name), "clearing a writable bundle succeeds")
            report(![root, macOS, server].contains { Quarantine.isSet($0, attribute: name) },
                   "clearing reaches the bundle and every file inside it")
            report(Quarantine.clear(root, attribute: name), "clearing what is already clear is not a failure")
        } catch {
            report(false, "quarantine fixture", "\(error)")
        }
        let bundled = URL(fileURLWithPath: "/Applications/Orion.app/Contents/MacOS/orion-mcp")
        let empty = fm.temporaryDirectory.appendingPathComponent("orion no mcp \(UUID())")
        report(AssistantPanelModel.configurationNotice(in: empty, bundled: bundled, quarantined: true) != nil
               && AssistantPanelModel.configurationNotice(in: empty, bundled: bundled, quarantined: false) == nil,
               "a quarantined bundled server is a notice, a cleared one is not")
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
                // Through the real shell too: the JSON and the TOML string
                // carry quotes and a space, and must arrive as one argument.
                let server = root.appendingPathComponent("Orion \"x\".app/orion-mcp")
                let withServer = Process()
                let out2 = Pipe()
                withServer.executableURL = URL(fileURLWithPath: "/bin/zsh")
                withServer.arguments = AssistantProcess.buildArgv(for: command, server: server)
                withServer.environment = process.environment
                withServer.standardOutput = out2
                withServer.standardError = FileHandle.nullDevice
                try withServer.run()
                let text2 = String(decoding: out2.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
                withServer.waitUntilExit()
                let flag = command == "claude"
                    ? "--mcp-config\n" + AssistantProcess.claudeConfig(server) + "\n"
                    : "-c\nmcp_servers.orion.command=\""
                report(text2.contains(flag), "\(command) receives the bundled server as one literal argument")
            }
            let bundled = URL(fileURLWithPath: "/Applications/Orion.app/Contents/MacOS/orion-mcp")
            report(AssistantPanelModel.configurationNotice(in: root, bundled: nil) != nil,
                   "missing MCP configuration reports how to choose a tools folder")
            report(AssistantPanelModel.configurationNotice(in: root, bundled: bundled) == nil
                   && AssistantPanelModel.launchServer(in: root, bundled: bundled) == bundled,
                   "a bundled server needs no tools folder and is launched")
            try "{}".write(to: root.appendingPathComponent(".mcp.json"), atomically: true, encoding: .utf8)
            report(AssistantPanelModel.configurationNotice(in: root, bundled: nil) == nil,
                   "configured tools folder clears the notice")
            report(AssistantPanelModel.launchServer(in: root, bundled: bundled) == nil,
                   "a tools folder's .mcp.json wins over the bundled server, never both")
        } catch {
            report(false, "assistant launch fixture", "\(error)")
        }
    }
}
