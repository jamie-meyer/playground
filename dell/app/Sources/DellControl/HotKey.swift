import AppKit
import Carbon.HIToolbox
import Foundation

struct HotKey: Equatable {
    let keyCode: UInt32
    let modifiers: UInt32
    let keyLabel: String

    static let defaultShortcut = HotKey(
        keyCode: 0,
        modifiers: UInt32(optionKey),
        keyLabel: "A"
    )

    var displayString: String {
        var value = ""
        if modifiers & UInt32(controlKey) != 0 {
            value += "⌃"
        }
        if modifiers & UInt32(optionKey) != 0 {
            value += "⌥"
        }
        if modifiers & UInt32(shiftKey) != 0 {
            value += "⇧"
        }
        if modifiers & UInt32(cmdKey) != 0 {
            value += "⌘"
        }
        return value + keyLabel
    }

    static func from(event: NSEvent) -> HotKey? {
        let modifiers = carbonModifiers(from: event.modifierFlags)
        guard modifiers != 0 else {
            return nil
        }

        let keyCode = UInt32(event.keyCode)
        let keyLabel = keyLabel(
            for: event.keyCode,
            characters: event.charactersIgnoringModifiers
        )
        guard !keyLabel.isEmpty else {
            return nil
        }

        return HotKey(
            keyCode: keyCode,
            modifiers: modifiers,
            keyLabel: keyLabel
        )
    }

    private static func carbonModifiers(
        from flags: NSEvent.ModifierFlags
    ) -> UInt32 {
        var modifiers: UInt32 = 0
        if flags.contains(.control) {
            modifiers |= UInt32(controlKey)
        }
        if flags.contains(.option) {
            modifiers |= UInt32(optionKey)
        }
        if flags.contains(.shift) {
            modifiers |= UInt32(shiftKey)
        }
        if flags.contains(.command) {
            modifiers |= UInt32(cmdKey)
        }
        return modifiers
    }

    private static func keyLabel(
        for keyCode: UInt16,
        characters: String?
    ) -> String {
        if let known = knownKeyLabels[keyCode] {
            return known
        }
        guard let characters, !characters.isEmpty else {
            return ""
        }
        return characters.uppercased()
    }

    private static let knownKeyLabels: [UInt16: String] = [
        0: "A", 1: "S", 2: "D", 3: "F", 4: "H", 5: "G",
        6: "Z", 7: "X", 8: "C", 9: "V", 11: "B", 12: "Q",
        13: "W", 14: "E", 15: "R", 16: "Y", 17: "T",
        18: "1", 19: "2", 20: "3", 21: "4", 22: "6",
        23: "5", 24: "=", 25: "9", 26: "7", 27: "-",
        28: "8", 29: "0", 30: "]", 31: "O", 32: "U",
        33: "[", 34: "I", 35: "P", 36: "↩", 37: "L",
        38: "J", 39: "'", 40: "K", 41: ";", 42: "\\",
        43: ",", 44: "/", 45: "N", 46: "M", 47: ".",
        48: "⇥", 49: "Space", 50: "`", 51: "⌫", 53: "⎋",
        65: ".", 67: "*", 69: "+", 71: "Clear", 75: "/",
        76: "⌤", 78: "-", 81: "=", 82: "0", 83: "1",
        84: "2", 85: "3", 86: "4", 87: "5", 88: "6",
        89: "7", 91: "8", 92: "9", 96: "F5", 97: "F6",
        98: "F7", 99: "F3", 100: "F8", 101: "F9",
        103: "F11", 105: "F13", 106: "F16", 107: "F14",
        109: "F10", 111: "F12", 113: "F15", 114: "Help",
        115: "Home", 116: "Page Up", 117: "⌦", 118: "F4",
        119: "End", 120: "F2", 121: "Page Down", 122: "F1",
        123: "←", 124: "→", 125: "↓", 126: "↑",
    ]
}
