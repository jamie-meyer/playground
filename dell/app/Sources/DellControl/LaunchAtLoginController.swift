import Foundation
import ServiceManagement

final class LaunchAtLoginController {
    private enum Key {
        static let attemptedInitialRegistration =
            "attemptedInitialLaunchAtLoginRegistration"
        static let registeredBundlePath = "launchAtLoginRegisteredBundlePath"
        static let pendingPathMigration =
            "launchAtLoginPendingPathMigration"
    }

    private let defaults: UserDefaults
    private let service = SMAppService.mainApp
    private let bundleURL: URL

    init(
        defaults: UserDefaults = .standard,
        bundleURL: URL = Bundle.main.bundleURL
    ) {
        self.defaults = defaults
        self.bundleURL = bundleURL
    }

    var status: SMAppService.Status {
        service.status
    }

    var isInstalledInApplications: Bool {
        Self.isInstalledApplicationURL(bundleURL)
    }

    var isEnabledOrAwaitingApproval: Bool {
        isInstalledInApplications &&
            (status == .enabled || status == .requiresApproval)
    }

    var statusDescription: String {
        guard isInstalledInApplications else {
            return "Move Dell Control to Applications before enabling Launch at Login."
        }
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
        guard isInstalledInApplications else {
            return
        }

        let path = installedBundlePath
        let previousPath = defaults.string(
            forKey: Key.registeredBundlePath
        )

        let hasPendingMigration = defaults.bool(
            forKey: Key.pendingPathMigration
        )
        if previousPath != path &&
            (hasPendingMigration ||
                status == .enabled ||
                status == .requiresApproval ||
                status == .notFound) {
            defaults.set(true, forKey: Key.pendingPathMigration)
            do {
                if status == .enabled || status == .requiresApproval {
                    try service.unregister()
                }
                try service.register()
                recordSuccessfulRegistration(path: path)
            } catch {
                // Keep the migration marker so the installed app retries on
                // its next launch if macOS temporarily refuses the refresh.
            }
            return
        }

        guard !defaults.bool(
            forKey: Key.attemptedInitialRegistration
        ) else {
            return
        }
        do {
            try setEnabled(true)
        } catch {
            // Leave the marker unset so moving the app to /Applications and
            // relaunching can try again.
        }
    }

    func setEnabled(_ enabled: Bool) throws {
        if enabled {
            guard isInstalledInApplications else {
                throw LaunchAtLoginError.appIsNotInstalled
            }
            if status == .notRegistered || status == .notFound {
                try service.register()
            }
            recordSuccessfulRegistration(path: installedBundlePath)
        } else if status == .enabled || status == .requiresApproval {
            try service.unregister()
            defaults.removeObject(forKey: Key.pendingPathMigration)
        }
    }

    func openSystemSettings() {
        SMAppService.openSystemSettingsLoginItems()
    }

    static func isInstalledApplicationURL(_ url: URL) -> Bool {
        let path = url.resolvingSymlinksInPath().standardizedFileURL.path
        return path == "/Applications" || path.hasPrefix("/Applications/")
    }

    private var installedBundlePath: String {
        bundleURL.resolvingSymlinksInPath().standardizedFileURL.path
    }

    private func recordSuccessfulRegistration(path: String) {
        defaults.set(true, forKey: Key.attemptedInitialRegistration)
        defaults.set(path, forKey: Key.registeredBundlePath)
        defaults.removeObject(forKey: Key.pendingPathMigration)
    }
}

private enum LaunchAtLoginError: LocalizedError {
    case appIsNotInstalled

    var errorDescription: String? {
        switch self {
        case .appIsNotInstalled:
            return "Move Dell Control to the Applications folder, open that copy, and then enable Launch at Login."
        }
    }
}
