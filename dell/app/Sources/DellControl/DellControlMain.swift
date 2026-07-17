import AppKit

@main
enum DellControlMain {
    static func main() {
        if CommandLine.arguments.contains("--self-test") {
            runSelfTest()
            return
        }

        let application = NSApplication.shared
        let delegate = AppDelegate()
        application.delegate = delegate
        application.run()
        withExtendedLifetime(delegate) {}
    }

    private static func runSelfTest() {
        precondition(
            HotKey.defaultShortcut.displayString == "⌥A",
            "Default hotkey does not render as Option-A."
        )
        precondition(
            Set(MonitorSource.allCases.map(\.rawValue)) ==
                Set(["usb-c", "dp1", "dp2", "hdmi1", "hdmi2"]),
            "Monitor source catalog is incomplete."
        )
        precondition(
            LaunchAtLoginController.isInstalledApplicationURL(
                URL(fileURLWithPath: "/Applications/Dell Control.app")
            ),
            "Applications-folder detection rejected an installed app."
        )
        precondition(
            !LaunchAtLoginController.isInstalledApplicationURL(
                URL(fileURLWithPath: "/Users/example/Downloads/Dell Control.app")
            ),
            "Applications-folder detection accepted a downloaded app."
        )
        let helperURL = Bundle.main.bundleURL
            .appendingPathComponent("Contents/Helpers/dellctl")
        precondition(
            FileManager.default.isExecutableFile(atPath: helperURL.path),
            "The bundled dellctl helper is missing or not executable."
        )
        print("DellControl self-test: passed")
    }
}
