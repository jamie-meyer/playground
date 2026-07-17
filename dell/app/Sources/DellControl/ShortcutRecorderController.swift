import AppKit

final class ShortcutRecorderController: NSWindowController {
    private let recorderView = ShortcutRecorderView()
    private let promptLabel = NSTextField(
        labelWithString: "Press the new shortcut"
    )
    private var completion: ((HotKey?) -> Void)?

    init() {
        let panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: 360, height: 150),
            styleMask: [.titled],
            backing: .buffered,
            defer: false
        )
        panel.title = "Record Hotkey"
        panel.isReleasedWhenClosed = false
        super.init(window: panel)
        buildContent()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    func beginSheet(
        for parent: NSWindow,
        completion: @escaping (HotKey?) -> Void
    ) {
        self.completion = completion
        promptLabel.stringValue = "Press the new shortcut"
        guard let window else {
            completion(nil)
            return
        }
        parent.beginSheet(window) { [weak self] _ in
            self?.completion = nil
        }
        window.makeFirstResponder(recorderView)
    }

    @objc private func cancel() {
        finish(with: nil)
    }

    private func finish(with hotKey: HotKey?) {
        guard let window, let parent = window.sheetParent else {
            completion?(hotKey)
            return
        }
        let callback = completion
        parent.endSheet(window)
        callback?(hotKey)
    }

    private func buildContent() {
        guard let window else {
            return
        }

        promptLabel.font = .systemFont(ofSize: 18, weight: .semibold)
        promptLabel.alignment = .center

        let explanation = NSTextField(
            wrappingLabelWithString:
                "Use at least one modifier (⌃, ⌥, ⇧, or ⌘). Escape cancels."
        )
        explanation.alignment = .center
        explanation.textColor = .secondaryLabelColor

        let cancelButton = NSButton(
            title: "Cancel",
            target: self,
            action: #selector(cancel)
        )
        cancelButton.keyEquivalent = "\u{1b}"

        let stack = NSStackView(
            views: [promptLabel, explanation, cancelButton]
        )
        stack.orientation = .vertical
        stack.alignment = .centerX
        stack.spacing = 14
        stack.translatesAutoresizingMaskIntoConstraints = false

        recorderView.translatesAutoresizingMaskIntoConstraints = false
        recorderView.onKeyDown = { [weak self] event in
            guard let self else {
                return
            }
            if event.keyCode == 53,
               event.modifierFlags.intersection(
                   [.command, .control, .option, .shift]
               ).isEmpty {
                self.finish(with: nil)
                return
            }
            guard let hotKey = HotKey.from(event: event) else {
                NSSound.beep()
                self.promptLabel.stringValue = "Include a modifier"
                return
            }
            self.finish(with: hotKey)
        }

        recorderView.addSubview(stack)
        window.contentView = recorderView
        NSLayoutConstraint.activate([
            stack.centerXAnchor.constraint(
                equalTo: recorderView.centerXAnchor
            ),
            stack.centerYAnchor.constraint(
                equalTo: recorderView.centerYAnchor
            ),
            stack.leadingAnchor.constraint(
                greaterThanOrEqualTo: recorderView.leadingAnchor,
                constant: 24
            ),
            stack.trailingAnchor.constraint(
                lessThanOrEqualTo: recorderView.trailingAnchor,
                constant: -24
            ),
        ])
    }
}

private final class ShortcutRecorderView: NSView {
    var onKeyDown: ((NSEvent) -> Void)?

    override var acceptsFirstResponder: Bool {
        true
    }

    override func keyDown(with event: NSEvent) {
        onKeyDown?(event)
    }
}
