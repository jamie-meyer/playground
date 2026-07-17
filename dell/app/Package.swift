// swift-tools-version: 5.10

import PackageDescription

let package = Package(
    name: "DellControl",
    platforms: [
        .macOS(.v13),
    ],
    products: [
        .executable(name: "DellControl", targets: ["DellControl"]),
    ],
    targets: [
        .executableTarget(
            name: "DellControl",
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("Carbon"),
                .linkedFramework("ServiceManagement"),
            ]
        ),
    ]
)
