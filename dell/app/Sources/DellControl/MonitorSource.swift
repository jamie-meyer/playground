import Foundation

enum MonitorSource: String, CaseIterable {
    case usbC = "usb-c"
    case displayPort1 = "dp1"
    case displayPort2 = "dp2"
    case hdmi1
    case hdmi2

    var displayName: String {
        switch self {
        case .usbC:
            return "USB-C"
        case .displayPort1:
            return "DisplayPort 1"
        case .displayPort2:
            return "DisplayPort 2"
        case .hdmi1:
            return "HDMI 1"
        case .hdmi2:
            return "HDMI 2"
        }
    }
}
