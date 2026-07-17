import Carbon.HIToolbox
import Foundation

enum GlobalHotKeyError: LocalizedError {
    case eventHandler(OSStatus)
    case registration(OSStatus)

    var errorDescription: String? {
        switch self {
        case let .eventHandler(status):
            return "Could not install the hotkey event handler (\(status))."
        case let .registration(status):
            if status == eventHotKeyExistsErr {
                return "That shortcut is already reserved by another app."
            }
            return "Could not register the shortcut (\(status))."
        }
    }
}

final class GlobalHotKey {
    private var eventHandler: EventHandlerRef?
    private var hotKeyReference: EventHotKeyRef?
    private var action: (() -> Void)?
    private let hotKeyID = EventHotKeyID(
        signature: fourCharacterCode("DCTL"),
        id: 1
    )

    init() throws {
        var eventType = EventTypeSpec(
            eventClass: OSType(kEventClassKeyboard),
            eventKind: UInt32(kEventHotKeyPressed)
        )
        let userData = Unmanaged.passUnretained(self).toOpaque()
        let status = InstallEventHandler(
            GetApplicationEventTarget(),
            { _, event, userData in
                guard let event, let userData else {
                    return OSStatus(eventNotHandledErr)
                }
                let manager = Unmanaged<GlobalHotKey>
                    .fromOpaque(userData)
                    .takeUnretainedValue()
                return manager.handle(event: event)
            },
            1,
            &eventType,
            userData,
            &eventHandler
        )
        guard status == noErr else {
            throw GlobalHotKeyError.eventHandler(status)
        }
    }

    deinit {
        unregister()
        if let eventHandler {
            RemoveEventHandler(eventHandler)
        }
    }

    func register(_ hotKey: HotKey, action: @escaping () -> Void) throws {
        unregister()
        self.action = action

        var reference: EventHotKeyRef?
        let status = RegisterEventHotKey(
            hotKey.keyCode,
            hotKey.modifiers,
            hotKeyID,
            GetApplicationEventTarget(),
            OptionBits(kEventHotKeyExclusive),
            &reference
        )
        guard status == noErr else {
            self.action = nil
            throw GlobalHotKeyError.registration(status)
        }
        hotKeyReference = reference
    }

    func unregister() {
        if let hotKeyReference {
            UnregisterEventHotKey(hotKeyReference)
            self.hotKeyReference = nil
        }
        action = nil
    }

    private func handle(event: EventRef) -> OSStatus {
        var receivedID = EventHotKeyID()
        let status = GetEventParameter(
            event,
            EventParamName(kEventParamDirectObject),
            EventParamType(typeEventHotKeyID),
            nil,
            MemoryLayout<EventHotKeyID>.size,
            nil,
            &receivedID
        )
        guard status == noErr, receivedID.id == hotKeyID.id,
              receivedID.signature == hotKeyID.signature else {
            return OSStatus(eventNotHandledErr)
        }
        action?()
        return noErr
    }
}

private func fourCharacterCode(_ value: String) -> FourCharCode {
    value.utf8.reduce(0) { result, character in
        (result << 8) + FourCharCode(character)
    }
}
