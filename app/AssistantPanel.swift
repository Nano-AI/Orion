// The assistant column: a toggleable strip at the leading edge of the window
// that runs the photographer's coding agent (`claude` by default, `codex`
// selectable) inside Orion, so they can talk to the model without leaving
// the app.
//
// Orion publishes the open photo to
// `~/Library/Application Support/Orion/current.json` on its own (see
// Engine+Document.swift) and the Orion MCP server reads it from there, so the
// agent only needs to be launched somewhere that server is registered — see
// `AssistantPanelModel.workingDirectory` in AssistantProcess.swift, which is
// `.mcp.json`'s directory.
//
// Terminal hosting is third_party/SwiftTerm (MIT, vendored — see
// third_party/SwiftTerm/VERSION), compiled straight into this target, so its
// types (`LocalProcessTerminalView`, `TerminalView`) are plain internal names
// here rather than an import.

import AppKit
import SwiftUI

/// Hosts one `SwiftTerm.LocalProcessTerminalView` running `/bin/zsh -l -i -c`
/// in `workingDirectory`. Both .zprofile and .zshrc must load to find CLI
/// installs (native installers, nvm, Homebrew) when Orion opens from Finder.
///
/// That shell wrapper is also what keeps a missing `claude`/`codex` from
/// crashing anything: `startProcess` always launches `/bin/zsh` itself, which
/// always succeeds, and *it* is what looks up the inner command — so "command
/// not found" prints into the terminal like any other shell error instead of
/// failing to launch at the SwiftTerm level.
struct AssistantTerminalView: NSViewRepresentable {
    let command: String
    let workingDirectory: URL
    /// Changes on every tap of the header's restart button, so a relaunch can
    /// be forced even when neither `command` nor `workingDirectory` changed.
    let restartToken: UUID
    /// ⌘=/⌘-/⌘0 while focused — see `handleTerminalFontShortcut` in
    /// OrionApp+Commands.swift.
    let fontSize: Double

    /// Remembers what is currently running so `updateNSView` only relaunches
    /// when something actually changed, applies `fontSize` without also
    /// relaunching, hands the terminal first responder when it appears and
    /// gives it back on the way out, and terminates the child on window
    /// close and on app quit — the two ways the column's NSView can go away
    /// without SwiftUI calling `dismantleNSView` for the second one.
    final class Coordinator {
        weak var view: LocalProcessTerminalView?
        var launchedCommand: String?
        var launchedDirectory: URL?
        var launchedToken: UUID?
        var appliedFontSize: Double?
        private var quitObserver: NSObjectProtocol?
        /// What held first responder before the terminal took it, so closing
        /// the column hands focus back rather than leaving the window
        /// pointed at a view that is about to be deallocated.
        private weak var priorResponder: NSResponder?

        init() {
            quitObserver = NotificationCenter.default.addObserver(
                forName: NSApplication.willTerminateNotification,
                object: nil, queue: nil
            ) { [weak self] _ in self?.view?.terminate() }
        }

        deinit {
            if let quitObserver { NotificationCenter.default.removeObserver(quitObserver) }
        }

        /// Called once the view has an actual `NSWindow` — `makeNSView` runs
        /// before SwiftUI inserts the view into the hierarchy, so this is
        /// dispatched to the next run-loop turn rather than called directly.
        func takeFocusIfNeeded(_ view: LocalProcessTerminalView) {
            guard let window = view.window, window.firstResponder !== view else { return }
            priorResponder = window.firstResponder
            window.makeFirstResponder(view)
        }

        func releaseFocus(_ view: LocalProcessTerminalView) {
            guard let window = view.window, window.firstResponder === view else { return }
            window.makeFirstResponder(priorResponder)
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> LocalProcessTerminalView {
        let view = LocalProcessTerminalView(frame: .zero)
        context.coordinator.view = view
        relaunchIfNeeded(view, context: context)
        applyFontIfNeeded(view, context: context)
        DispatchQueue.main.async { [coordinator = context.coordinator] in
            coordinator.takeFocusIfNeeded(view)
        }
        return view
    }

    func updateNSView(_ view: LocalProcessTerminalView, context: Context) {
        relaunchIfNeeded(view, context: context)
        applyFontIfNeeded(view, context: context)
    }

    static func dismantleNSView(_ view: LocalProcessTerminalView, coordinator: Coordinator) {
        coordinator.releaseFocus(view)
        view.terminate()
    }

    private func relaunchIfNeeded(_ view: LocalProcessTerminalView, context: Context) {
        guard context.coordinator.launchedCommand != command
            || context.coordinator.launchedDirectory != workingDirectory
            || context.coordinator.launchedToken != restartToken else { return }
        if context.coordinator.launchedCommand != nil { view.terminate() }
        context.coordinator.launchedCommand = command
        context.coordinator.launchedDirectory = workingDirectory
        context.coordinator.launchedToken = restartToken
        let args = AssistantProcess.buildArgv(for: command)
        view.startProcess(executable: "/bin/zsh",
                           args: args,
                           currentDirectory: workingDirectory.path)
    }

    /// Guarded on the last-applied size rather than set unconditionally:
    /// `TerminalView.font`'s setter also clears the active selection, so
    /// writing it on every SwiftUI update (most of which have nothing to do
    /// with the font) would drop a selection the photographer is mid-drag on.
    private func applyFontIfNeeded(_ view: LocalProcessTerminalView, context: Context) {
        guard context.coordinator.appliedFontSize != fontSize else { return }
        context.coordinator.appliedFontSize = fontSize
        view.font = NSFont.monospacedSystemFont(ofSize: fontSize, weight: .regular)
    }
}

/// The column chrome: a header with the claude/codex popup and a restart
/// button, the terminal, and a drag-to-resize handle on the trailing edge
/// (the column sits at the window's leading edge, so its own trailing edge
/// is the one touching the rest of the window).
struct AssistantColumn: View {
    @Bindable var model: AssistantPanelModel
    let workingDirectory: URL

    private let minWidth: Double = 360
    private let maxWidth: Double = 800
    @State private var dragStartWidth: Double?
    @State private var isHoveringHandle = false
    @State private var resizeCursorPushed = false

    var body: some View {
        HStack(spacing: 0) {
            VStack(spacing: 0) {
                header
                Rectangle().fill(Palette.line).frame(height: 1)
                AssistantTerminalView(command: model.command,
                                      workingDirectory: workingDirectory,
                                      restartToken: model.restartToken,
                                      fontSize: model.fontSize)
            }
            .frame(width: model.width)
            .background(Palette.panel)
            resizeHandle
        }
    }

    private var resizeHandle: some View {
        ZStack {
            Rectangle().fill(Palette.line).frame(width: 1)
        }
        .frame(width: 8) // wider than the visible line, for an easier drag target
        .contentShape(Rectangle())
        .onHover { inside in
            isHoveringHandle = inside
            syncResizeCursor()
        }
        .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        let start = dragStartWidth ?? model.width
                        dragStartWidth = start
                        syncResizeCursor()
                        // The column is at the leading edge, so dragging the
                        // handle right (positive translation) makes it wider.
                        model.width = min(max(start + value.translation.width,
                                               minWidth), maxWidth)
                    }
                    .onEnded { _ in
                        dragStartWidth = nil
                        syncResizeCursor()
                    }
            )
    }

    /// One push/pop pair kept in sync with hover-or-drag, rather than one
    /// push/pop per gesture phase: `NSCursor.push`/`pop` is a stack, and
    /// pushing on both hover and drag-start without matching pops would
    /// leave the resize cursor stuck after the drag ends.
    private func syncResizeCursor() {
        let active = isHoveringHandle || dragStartWidth != nil
        if active && !resizeCursorPushed {
            NSCursor.resizeLeftRight.push()
            resizeCursorPushed = true
        } else if !active && resizeCursorPushed {
            NSCursor.pop()
            resizeCursorPushed = false
        }
    }

    private var header: some View {
        HStack(spacing: 8) {
            Picker("", selection: $model.command) {
                ForEach(AssistantPanelModel.commands, id: \.self) { Text($0) }
            }
            .pickerStyle(.menu)
            .frame(width: 110)
            .labelsHidden()

            Button { model.restart() } label: {
                Image(systemName: "arrow.clockwise")
            }
            .buttonStyle(.plain)
            .foregroundStyle(Palette.dim)
            .help("Restart")

            Button("Choose tools folder", systemImage: "folder.badge.gearshape") {
                chooseToolsFolder()
            }
            .labelStyle(.iconOnly)
            .buttonStyle(.plain)
            .foregroundStyle(Palette.dim)
            .help("Choose the assistant’s working folder (.mcp.json)")

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

    private func chooseToolsFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.directoryURL = model.workingDirectory
        panel.message = "Choose the folder containing Orion’s .mcp.json. The assistant will restart in this folder."
        panel.prompt = "Choose"
        panel.begin { response in
            if response == .OK, let directory = panel.url {
                model.setWorkingDirectory(directory)
            }
        }
    }
}

/// Toggling the column from the menu. A plain shared instance rather than
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
