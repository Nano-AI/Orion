// The assistant drawer: a toggleable strip under the canvas that runs the
// photographer's coding agent (`claude` by default, `codex` selectable)
// inside Orion, so they can talk to the model without leaving the app.
//
// Orion publishes the open photo to
// `~/Library/Application Support/Orion/current.json` on its own (see
// Engine+Document.swift) and the Orion MCP server reads it from there, so the
// agent only needs to be launched somewhere that server is registered — see
// `resolveWorkingDirectory` in AssistantProcess.swift, which is `.mcp.json`'s
// directory.
//
// Terminal hosting is third_party/SwiftTerm (MIT, vendored — see
// third_party/SwiftTerm/VERSION), compiled straight into this target, so its
// types (`LocalProcessTerminalView`, `TerminalView`) are plain internal names
// here rather than an import.

import AppKit
import SwiftUI

/// Hosts one `SwiftTerm.LocalProcessTerminalView` running `/bin/zsh -l -c
/// <command>` in `workingDirectory`. A login shell so PATH resolves
/// GUI-app-blind installs (nvm, a Homebrew shim set in .zprofile, ...) the
/// way a real Terminal.app window would.
///
/// That shell wrapper is also what keeps a missing `claude`/`codex` from
/// crashing anything: `startProcess` always launches `/bin/zsh` itself, which
/// always succeeds, and *it* is what looks up the inner command — so "command
/// not found" prints into the terminal like any other shell error instead of
/// failing to launch at the SwiftTerm level.
struct AssistantTerminalView: NSViewRepresentable {
    let command: String
    let workingDirectory: URL

    /// Remembers what is currently running so `updateNSView` only relaunches
    /// when the popup or working directory actually changed, and terminates
    /// the child on window close and on app quit — the two ways the drawer's
    /// NSView can go away without SwiftUI calling `dismantleNSView` for the
    /// second one.
    final class Coordinator {
        weak var view: LocalProcessTerminalView?
        var launchedCommand: String?
        var launchedDirectory: URL?
        private var quitObserver: NSObjectProtocol?

        init() {
            quitObserver = NotificationCenter.default.addObserver(
                forName: NSApplication.willTerminateNotification,
                object: nil, queue: nil
            ) { [weak self] _ in self?.view?.terminate() }
        }

        deinit {
            if let quitObserver { NotificationCenter.default.removeObserver(quitObserver) }
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> LocalProcessTerminalView {
        let view = LocalProcessTerminalView(frame: .zero)
        context.coordinator.view = view
        relaunchIfNeeded(view, context: context)
        return view
    }

    func updateNSView(_ view: LocalProcessTerminalView, context: Context) {
        relaunchIfNeeded(view, context: context)
    }

    static func dismantleNSView(_ view: LocalProcessTerminalView, coordinator: Coordinator) {
        view.terminate()
    }

    private func relaunchIfNeeded(_ view: LocalProcessTerminalView, context: Context) {
        guard context.coordinator.launchedCommand != command
            || context.coordinator.launchedDirectory != workingDirectory else { return }
        if context.coordinator.launchedCommand != nil { view.terminate() }
        context.coordinator.launchedCommand = command
        context.coordinator.launchedDirectory = workingDirectory
        view.startProcess(executable: "/bin/zsh",
                           args: ["-l", "-c", command],
                           currentDirectory: workingDirectory.path)
    }
}

/// The drawer chrome: a drag-to-resize handle, a header with the claude/codex
/// popup, and the terminal itself.
struct AssistantDrawer: View {
    @Bindable var model: AssistantPanelModel
    let workingDirectory: URL

    private let minHeight: Double = 160
    private let maxHeight: Double = 640
    @State private var dragStartHeight: Double?

    var body: some View {
        VStack(spacing: 0) {
            resizeHandle
            header
            Rectangle().fill(Palette.line).frame(height: 1)
            AssistantTerminalView(command: model.command, workingDirectory: workingDirectory)
        }
        .frame(height: model.height)
        .background(Palette.panel)
    }

    private var resizeHandle: some View {
        Rectangle()
            .fill(Palette.line)
            .frame(height: 4)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        let start = dragStartHeight ?? model.height
                        dragStartHeight = start
                        // The drawer grows from the bottom, so dragging the
                        // handle up (negative translation) makes it taller.
                        model.height = min(max(start - value.translation.height,
                                                minHeight), maxHeight)
                    }
                    .onEnded { _ in dragStartHeight = nil }
            )
    }

    private var header: some View {
        HStack(spacing: 8) {
            Picker("", selection: $model.command) {
                ForEach(AssistantPanelModel.commands, id: \.self) { Text($0) }
            }
            .pickerStyle(.menu)
            .frame(width: 110)
            .labelsHidden()

            if let notice = model.notice {
                Text(notice).font(.caption).foregroundStyle(Palette.dim)
            }

            Spacer()

            Button { model.isOpen = false } label: {
                Image(systemName: "xmark")
            }
            .buttonStyle(.plain)
            .foregroundStyle(Palette.dim)
        }
        .padding(.horizontal, 10)
        .frame(height: 28)
    }
}

/// Toggling the drawer from the menu. A plain shared instance rather than
/// `@FocusedValue` — Orion is effectively single-window (`CommandGroup
/// (replacing: .newItem) {}` in OrionApp.swift disables New Window), so
/// there is exactly one `AssistantPanelModel` and no window to disambiguate.
struct AssistantCommands: Commands {
    let model: AssistantPanelModel

    var body: some Commands {
        CommandMenu("Assistant") {
            Button(model.isOpen ? "Hide Assistant" : "Show Assistant") {
                model.toggle()
            }
            // ⌘⇧A is already "Deselect" on the Photo menu (OrionApp+Commands
            // .swift) — ⌘⌥A instead.
            .keyboardShortcut("a", modifiers: [.command, .option])
        }
    }
}
