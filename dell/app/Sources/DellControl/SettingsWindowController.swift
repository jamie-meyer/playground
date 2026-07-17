import AppKit
import ServiceManagement

final class SettingsWindowController: NSWindowController {
    var onSettingsChanged: (() -> Void)?
    var onSwitchNow: (() -> Void)?

    private let preferences: AppPreferences
    private let launchAtLogin: LaunchAtLoginController
    private let targetPopup = NSPopUpButton()
    private let hotKeyEnabled = NSButton(
        checkboxWithTitle: "Enable global hotkey",
        target: nil,
        action: nil
    )
    private let hotKeyValue = NSTextField(labelWithString: "")
    private let launchAtLoginButton = NSButton(
        checkboxWithTitle: "Launch at Login",
        target: nil,
        action: nil
    )
    private let loginStatus = NSTextField(labelWithString: "")
    private let switchStatus = NSTextField(labelWithString: "Ready")
    private var recorderController: ShortcutRecorderController?

    init(
        preferences: AppPreferences,
        launchAtLogin: LaunchAtLoginController
    ) {
        self.preferences = preferences
        self.launchAtLogin = launchAtLogin

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 460, height: 340),
            styleMask: [.titled, .closable],
            backing: .buffered,
            defer: false
        )
        window.title = "Dell Control Settings"
        window.isReleasedWhenClosed = false
        window.center()
        super.init(window: window)
        buildContent()
        refresh()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    override func showWindow(_ sender: Any?) {
        refresh()
        super.showWindow(sender)
        NSApp.activate(ignoringOtherApps: true)
        window?.makeKeyAndOrderFront(sender)
    }

    func updateSwitchStatus(_ message: String) {
        switchStatus.stringValue = message
    }

    func refresh() {
        targetPopup.selectItem(
            withTag: MonitorSource.allCases.firstIndex(
                of: preferences.selectedSource
            ) ?? 0
        )
        hotKeyEnabled.state = preferences.isHotKeyEnabled ? .on : .off
        hotKeyValue.stringValue = preferences.hotKey.displayString
        switch launchAtLogin.status {
        case .enabled:
            launchAtLoginButton.state = .on
        case .requiresApproval:
            launchAtLoginButton.state = .mixed
        default:
            launchAtLoginButton.state = .off
        }
        loginStatus.stringValue = launchAtLogin.statusDescription
    }

    @objc private func selectTarget() {
        let index = targetPopup.indexOfSelectedItem
        guard MonitorSource.allCases.indices.contains(index) else {
            return
        }
        preferences.selectedSource = MonitorSource.allCases[index]
        onSettingsChanged?()
    }

    @objc private func toggleHotKey() {
        preferences.isHotKeyEnabled = hotKeyEnabled.state == .on
        onSettingsChanged?()
    }

    @objc private func recordHotKey() {
        guard let window else {
            return
        }
        let recorder = ShortcutRecorderController()
        recorderController = recorder
        recorder.beginSheet(for: window) { [weak self] hotKey in
            guard let self else {
                return
            }
            self.recorderController = nil
            if let hotKey {
                self.preferences.hotKey = hotKey
                self.preferences.isHotKeyEnabled = true
                self.refresh()
                self.onSettingsChanged?()
            }
        }
    }

    @objc private func toggleLaunchAtLogin() {
        do {
            if launchAtLogin.status == .requiresApproval {
                launchAtLogin.openSystemSettings()
            } else {
                try launchAtLogin.setEnabled(
                    launchAtLoginButton.state == .on
                )
            }
        } catch {
            let alert = NSAlert(error: error)
            alert.runModal()
        }
        refresh()
        onSettingsChanged?()
    }

    @objc private func switchNow() {
        onSwitchNow?()
    }

    private func buildContent() {
        guard let contentView = window?.contentView else {
            return
        }

        let heading = NSTextField(
            labelWithString: "Dell U4323QE"
        )
        heading.font = .systemFont(ofSize: 20, weight: .semibold)

        let summary = NSTextField(
            wrappingLabelWithString:
                "A focused menu-bar controller using the monitor's direct USB HID interface."
        )
        summary.textColor = .secondaryLabelColor

        for (index, source) in MonitorSource.allCases.enumerated() {
            targetPopup.addItem(withTitle: source.displayName)
            targetPopup.lastItem?.tag = index
        }
        targetPopup.target = self
        targetPopup.action = #selector(selectTarget)

        hotKeyEnabled.target = self
        hotKeyEnabled.action = #selector(toggleHotKey)

        hotKeyValue.font = .monospacedSystemFont(
            ofSize: 14,
            weight: .medium
        )
        hotKeyValue.alignment = .center
        hotKeyValue.wantsLayer = true
        hotKeyValue.layer?.cornerRadius = 5
        hotKeyValue.layer?.backgroundColor =
            NSColor.controlBackgroundColor.cgColor
        hotKeyValue.widthAnchor.constraint(equalToConstant: 82).isActive = true
        hotKeyValue.heightAnchor.constraint(equalToConstant: 26).isActive = true

        let recordButton = NSButton(
            title: "Record…",
            target: self,
            action: #selector(recordHotKey)
        )
        let hotKeyControls = NSStackView(
            views: [hotKeyValue, recordButton]
        )
        hotKeyControls.orientation = .horizontal
        hotKeyControls.spacing = 8

        launchAtLoginButton.allowsMixedState = true
        launchAtLoginButton.target = self
        launchAtLoginButton.action = #selector(toggleLaunchAtLogin)
        loginStatus.textColor = .secondaryLabelColor
        loginStatus.font = .systemFont(ofSize: 11)

        let loginControls = NSStackView(
            views: [launchAtLoginButton, loginStatus]
        )
        loginControls.orientation = .vertical
        loginControls.alignment = .leading
        loginControls.spacing = 3

        let targetLabel = NSTextField(labelWithString: "Target input:")
        let shortcutLabel = NSTextField(labelWithString: "Shortcut:")
        let launchLabel = NSTextField(labelWithString: "Startup:")
        for label in [targetLabel, shortcutLabel, launchLabel] {
            label.alignment = .right
        }

        let grid = NSGridView(views: [
            [targetLabel, targetPopup],
            [shortcutLabel, hotKeyControls],
            [launchLabel, loginControls],
        ])
        grid.rowSpacing = 14
        grid.columnSpacing = 14
        grid.column(at: 0).xPlacement = .trailing
        grid.column(at: 1).xPlacement = .leading

        switchStatus.textColor = .secondaryLabelColor
        switchStatus.lineBreakMode = .byTruncatingMiddle

        let switchButton = NSButton(
            title: "Switch Now",
            target: self,
            action: #selector(switchNow)
        )
        switchButton.keyEquivalent = "\r"

        let stack = NSStackView(
            views: [
                heading,
                summary,
                grid,
                switchStatus,
                switchButton,
            ]
        )
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 14
        stack.setCustomSpacing(22, after: summary)
        stack.translatesAutoresizingMaskIntoConstraints = false

        contentView.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(
                equalTo: contentView.leadingAnchor,
                constant: 28
            ),
            stack.trailingAnchor.constraint(
                equalTo: contentView.trailingAnchor,
                constant: -28
            ),
            stack.topAnchor.constraint(
                equalTo: contentView.topAnchor,
                constant: 24
            ),
            stack.bottomAnchor.constraint(
                lessThanOrEqualTo: contentView.bottomAnchor,
                constant: -24
            ),
            summary.widthAnchor.constraint(equalTo: stack.widthAnchor),
            grid.widthAnchor.constraint(equalTo: stack.widthAnchor),
            switchStatus.widthAnchor.constraint(equalTo: stack.widthAnchor),
        ])
    }
}
