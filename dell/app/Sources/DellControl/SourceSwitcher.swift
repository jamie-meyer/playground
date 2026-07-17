import Foundation

final class SourceSwitcher {
    enum State {
        case ready
        case switching(MonitorSource)
        case succeeded(MonitorSource)
        case failed(String)

        var message: String {
            switch self {
            case .ready:
                return "Ready"
            case let .switching(source):
                return "Switching to \(source.displayName)…"
            case let .succeeded(source):
                return "Switched to \(source.displayName)"
            case let .failed(message):
                return message
            }
        }
    }

    var onStateChange: ((State) -> Void)?

    private(set) var state: State = .ready {
        didSet {
            onStateChange?(state)
        }
    }
    private var process: Process?

    func switchInput(to source: MonitorSource) {
        guard process == nil else {
            return
        }

        guard let helperURL = helperURL() else {
            state = .failed(
                "The bundled dellctl helper could not be found."
            )
            return
        }

        let task = Process()
        let output = Pipe()
        task.executableURL = helperURL
        task.arguments = [
            "switch-input",
            "--enable-writes",
            source.rawValue,
        ]
        task.standardOutput = output
        task.standardError = output
        task.terminationHandler = { [weak self] completedTask in
            let data = output.fileHandleForReading.readDataToEndOfFile()
            let commandOutput = String(data: data, encoding: .utf8) ?? ""
            DispatchQueue.main.async {
                guard let self else {
                    return
                }
                self.process = nil
                if completedTask.terminationStatus == 0 {
                    self.state = .succeeded(source)
                } else {
                    let detail = commandOutput
                        .split(separator: "\n")
                        .last
                        .map(String.init)
                    self.state = .failed(
                        detail ?? "The monitor did not accept the switch."
                    )
                }
            }
        }

        do {
            state = .switching(source)
            process = task
            try task.run()
        } catch {
            process = nil
            state = .failed("Could not start dellctl: \(error.localizedDescription)")
        }
    }

    private func helperURL() -> URL? {
        if let override = ProcessInfo.processInfo.environment["DELLCTL_PATH"] {
            let url = URL(fileURLWithPath: override)
            if FileManager.default.isExecutableFile(atPath: url.path) {
                return url
            }
        }

        let bundled = Bundle.main.bundleURL
            .appendingPathComponent("Contents/Helpers/dellctl")
        if FileManager.default.isExecutableFile(atPath: bundled.path) {
            return bundled
        }
        return nil
    }
}
