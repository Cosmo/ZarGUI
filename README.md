# ZarGUI

A small drag-and-drop utility for macOS and Windows that packs a folder into a
[ZArchive](https://github.com/Exzap/ZArchive) (`.zar`) file: a read-only, zstd-compressed archive
with random access.

## Use

1. Drop a folder on the window (or use **Choose Folder…**).
2. The archive is written next to the folder as `<folder name>.zar`, or into the folder you pick under **Save in**.

It shows progress, can be cancelled, asks before replacing an existing archive, and never leaves a partial file behind.
ZarGUI only creates archives; it does not extract them.

## Build

The core is C++20 (`core/`), wrapped by a native UI on each platform: SwiftUI on macOS (`macos/`), WinUI 3 on Windows (`windows/`).

- **macOS** (Xcode, CMake, Ninja): `./scripts/build-macos.sh` produces `build/macos/ZarGUI.app` (universal, arm64 + x86_64).
- **Windows:** run `scripts\build-windows.cmd`. It installs missing tools through winget, builds, and writes
  `build\ZarGUI-Windows-x64.zip` and `build\ZarGUI-Windows-arm64.zip`. Unzip and run `ZarGUI.exe`.
- **Core tests:** `cmake -S . -B build/core && cmake --build build/core && ctest --test-dir build/core`

Build output and downloaded dependencies stay in `build/`.

## License

MIT, see [LICENSE](LICENSE). Third-party licenses are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
