import Foundation
import ServiceManagement

final class LaunchAtLoginController {
    private enum Key {
        static let attemptedInitialRegistration =
            "attemptedInitialLaunchAtLoginRegistration"
    }

    private let defaults: UserDefaults
    private let service = SMAppService.mainApp

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    var status: SMAppService.Status {
        service.status
    }

    var isEnabledOrAwaitingApproval: Bool {
        status == .enabled || status == .requiresApproval
    }

    var statusDescription: String {
        switch status {
        case .enabled:
            return "Launch at Login is on."
        case .requiresApproval:
            return "Launch at Login needs approval in System Settings."
        case .notRegistered:
            return "Launch at Login is off."
        case .notFound:
            return "Launch at Login is unavailable for this app location."
        @unknown default:
            return "Launch at Login status is unknown."
        }
    }

    func registerOnFirstLaunch() {
        guard !defaults.bool(
            forKey: Key.attemptedInitialRegistration
        ) else {
            return
        }
        do {
            try setEnabled(true)
            defaults.set(
                true,
                forKey: Key.attemptedInitialRegistration
            )
        } catch {
            // Leave the marker unset so moving the app to /Applications and
            // relaunching can try again.
        }
    }

    func setEnabled(_ enabled: Bool) throws {
        if enabled {
            if status == .notRegistered || status == .notFound {
                try service.register()
            }
        } else if status == .enabled || status == .requiresApproval {
            try service.unregister()
        }
    }

    func openSystemSettings() {
        SMAppService.openSystemSettingsLoginItems()
    }
}
