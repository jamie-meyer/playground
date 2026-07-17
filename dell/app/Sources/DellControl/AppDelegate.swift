import AppKit
import ServiceManagement

final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private let preferences = AppPreferences()
    private let launchAtLogin = LaunchAtLoginController()
    private let sourceSwitcher = SourceSwitcher()
    private var globalHotKey: GlobalHotKey?
    private var hotKeyError: String?
    private var statusItem: NSStatusItem?
    private var menu: NSMenu?
    private var targetMenuItems: [NSMenuItem] = []
    private var switchItem: NSMenuItem?
    private var hotKeyItem: NSMenuItem?
    private var launchAtLoginItem: NSMenuItem?
    private var statusMenuItem: NSMenuItem?
    private var settingsWindow: SettingsWindowController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        launchAtLogin.registerOnFirstLaunch()
        configureStatusItem()
        configureSettingsWindow()
        configureSourceSwitcher()
        applyHotKey()
        refreshMenu()
        if preferences.shouldShowInitialSettings {
            preferences.markInitialSettingsShown()
            showSettings()
        }
    }

    func menuNeedsUpdate(_ menu: NSMenu) {
        refreshMenu()
    }

    @objc private func switchNow() {
        sourceSwitcher.switchInput(to: preferences.selectedSource)
    }

    @objc private func selectTarget(_ sender: NSMenuItem) {
        guard let rawValue = sender.representedObject as? String,
              let source = MonitorSource(rawValue: rawValue) else {
            return
        }
        preferences.selectedSource = source
        settingsWindow?.refresh()
        refreshMenu()
    }

    @objc private func toggleHotKey() {
        preferences.isHotKeyEnabled.toggle()
        applyHotKey()
        settingsWindow?.refresh()
        refreshMenu()
    }

    @objc private func toggleLaunchAtLogin() {
        do {
            if launchAtLogin.status == .requiresApproval {
                launchAtLogin.openSystemSettings()
            } else {
                try launchAtLogin.setEnabled(
                    !launchAtLogin.isEnabledOrAwaitingApproval
                )
            }
        } catch {
            let alert = NSAlert(error: error)
            alert.runModal()
        }
        settingsWindow?.refresh()
        refreshMenu()
    }

    @objc private func showSettings() {
        settingsWindow?.showWindow(nil)
    }

    @objc private func quit() {
        NSApp.terminate(nil)
    }

    private func configureStatusItem() {
        let statusItem = NSStatusBar.system.statusItem(
            withLength: NSStatusItem.variableLength
        )
        statusItem.button?.image = NSImage(
            systemSymbolName: "display.2",
            accessibilityDescription: "Dell Control"
        )
        statusItem.button?.toolTip = "Dell Control"

        let menu = NSMenu()
        menu.delegate = self

        let switchItem = NSMenuItem(
            title: "Switch Now",
            action: #selector(switchNow),
            keyEquivalent: ""
        )
        switchItem.target = self
        menu.addItem(switchItem)
        menu.addItem(.separator())

        let targetItem = NSMenuItem(
            title: "Target Input",
            action: nil,
            keyEquivalent: ""
        )
        let targetMenu = NSMenu(title: "Target Input")
        for source in MonitorSource.allCases {
            let item = NSMenuItem(
                title: source.displayName,
                action: #selector(selectTarget(_:)),
                keyEquivalent: ""
            )
            item.target = self
            item.representedObject = source.rawValue
            targetMenu.addItem(item)
            targetMenuItems.append(item)
        }
        targetItem.submenu = targetMenu
        menu.addItem(targetItem)

        let hotKeyItem = NSMenuItem(
            title: "Enable Hotkey",
            action: #selector(toggleHotKey),
            keyEquivalent: ""
        )
        hotKeyItem.target = self
        menu.addItem(hotKeyItem)

        let launchAtLoginItem = NSMenuItem(
            title: "Launch at Login",
            action: #selector(toggleLaunchAtLogin),
            keyEquivalent: ""
        )
        launchAtLoginItem.target = self
        menu.addItem(launchAtLoginItem)
        menu.addItem(.separator())

        let statusMenuItem = NSMenuItem(
            title: "Ready",
            action: nil,
            keyEquivalent: ""
        )
        statusMenuItem.isEnabled = false
        menu.addItem(statusMenuItem)

        let settingsItem = NSMenuItem(
            title: "Settings…",
            action: #selector(showSettings),
            keyEquivalent: ","
        )
        settingsItem.target = self
        menu.addItem(settingsItem)
        menu.addItem(.separator())

        let quitItem = NSMenuItem(
            title: "Quit Dell Control",
            action: #selector(quit),
            keyEquivalent: "q"
        )
        quitItem.target = self
        menu.addItem(quitItem)

        statusItem.menu = menu
        self.statusItem = statusItem
        self.menu = menu
        self.switchItem = switchItem
        self.hotKeyItem = hotKeyItem
        self.launchAtLoginItem = launchAtLoginItem
        self.statusMenuItem = statusMenuItem
    }

    private func configureSettingsWindow() {
        let controller = SettingsWindowController(
            preferences: preferences,
            launchAtLogin: launchAtLogin
        )
        controller.onSettingsChanged = { [weak self] in
            self?.applyHotKey()
            self?.refreshMenu()
        }
        controller.onSwitchNow = { [weak self] in
            self?.switchNow()
        }
        settingsWindow = controller
    }

    private func configureSourceSwitcher() {
        sourceSwitcher.onStateChange = { [weak self] state in
            self?.statusMenuItem?.title = state.message
            self?.settingsWindow?.updateSwitchStatus(state.message)
            self?.statusItem?.button?.toolTip =
                "Dell Control — \(state.message)"
        }
    }

    private func applyHotKey() {
        globalHotKey?.unregister()
        hotKeyError = nil

        guard preferences.isHotKeyEnabled else {
            return
        }

        do {
            if globalHotKey == nil {
                globalHotKey = try GlobalHotKey()
            }
            try globalHotKey?.register(preferences.hotKey) { [weak self] in
                self?.switchNow()
            }
        } catch {
            hotKeyError = error.localizedDescription
        }
    }

    private func refreshMenu() {
        let source = preferences.selectedSource
        switchItem?.title = "Switch to \(source.displayName) Now"
        for item in targetMenuItems {
            item.state =
                item.representedObject as? String == source.rawValue
                ? .on
                : .off
        }

        if let hotKeyError {
            hotKeyItem?.title =
                "\(preferences.hotKey.displayString) Unavailable"
            hotKeyItem?.toolTip = hotKeyError
            hotKeyItem?.state = .off
        } else {
            hotKeyItem?.title =
                "Hotkey: \(preferences.hotKey.displayString)"
            hotKeyItem?.toolTip = nil
            hotKeyItem?.state =
                preferences.isHotKeyEnabled ? .on : .off
        }

        launchAtLoginItem?.state = {
            switch launchAtLogin.status {
            case .enabled:
                return .on
            case .requiresApproval:
                return .mixed
            default:
                return .off
            }
        }()
        launchAtLoginItem?.toolTip = launchAtLogin.statusDescription
    }
}
