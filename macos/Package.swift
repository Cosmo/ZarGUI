// swift-tools-version:5.9
import PackageDescription

// The C++ core is built by CMake (see scripts/build-macos.sh), which also
// passes `-Xlinker -L<dir>` so `-lzarpack` resolves to the universal library.
let package = Package(
    name: "ZarGUI",
    platforms: [.macOS(.v14)],
    targets: [
        .systemLibrary(name: "CZarpack"),
        .executableTarget(name: "ZarGUI", dependencies: ["CZarpack"]),
    ]
)
