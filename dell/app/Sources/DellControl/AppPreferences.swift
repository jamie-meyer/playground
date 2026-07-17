import Foundation

final class AppPreferences {
    private enum Key {
        static let selectedSource = "selectedSource"
        static let hotKeyCode = "hotKeyCode"
        static let hotKeyModifiers = "hotKeyModifiers"
        static let hotKeyLabel = "hotKeyLabel"
        static let hotKeyEnabled = "hotKeyEnabled"
        static let hasShownInitialSettings = "hasShownInitialSettings"
    }

    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        defaults.register(defaults: [
            Key.selectedSource: MonitorSource.usbC.rawValue,
            Key.hotKeyCode: Int(HotKey.defaultShortcut.keyCode),
            Key.hotKeyModifiers: Int(HotKey.defaultShortcut.modifiers),
            Key.hotKeyLabel: HotKey.defaultShortcut.keyLabel,
            Key.hotKeyEnabled: true,
        ])
    }

    var selectedSource: MonitorSource {
        get {
            let rawValue = defaults.string(forKey: Key.selectedSource)
            return MonitorSource(rawValue: rawValue ?? "") ?? .usbC
        }
        set {
            defaults.set(newValue.rawValue, forKey: Key.selectedSource)
        }
    }

    var hotKey: HotKey {
        get {
            HotKey(
                keyCode: UInt32(defaults.integer(forKey: Key.hotKeyCode)),
                modifiers: UInt32(
                    defaults.integer(forKey: Key.hotKeyModifiers)
                ),
                keyLabel: defaults.string(forKey: Key.hotKeyLabel) ?? "A"
            )
        }
        set {
            defaults.set(Int(newValue.keyCode), forKey: Key.hotKeyCode)
            defaults.set(
                Int(newValue.modifiers),
                forKey: Key.hotKeyModifiers
            )
            defaults.set(newValue.keyLabel, forKey: Key.hotKeyLabel)
        }
    }

    var isHotKeyEnabled: Bool {
        get {
            defaults.bool(forKey: Key.hotKeyEnabled)
        }
        set {
            defaults.set(newValue, forKey: Key.hotKeyEnabled)
        }
    }

    var shouldShowInitialSettings: Bool {
        !defaults.bool(forKey: Key.hasShownInitialSettings)
    }

    func markInitialSettingsShown() {
        defaults.set(true, forKey: Key.hasShownInitialSettings)
    }
}
