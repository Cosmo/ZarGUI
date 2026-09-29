# ZarGUI

A small drag-and-drop utility for macOS and Windows that packs a folder into a
[ZArchive](https://github.com/Exzap/ZArchive) (`.zar`) file: a read-only, zstd-compressed archive
with random access.

## Use

- **Create:** drop a folder on the window. The archive is written next to it as `<folder name>.zar`,
  or into the folder chosen under **Save in**.
- **View and extract:** drop a `.zar` file (or open one). It opens in its own window, where you can
  extract everything or just the selected files and folders.

- **Drag out:** drag files or folders from an archive window to extract just those items.

Both show progress, can be cancelled, ask before replacing existing files, and never leave partial files behind.
System files such as `.DS_Store`, `._*` and `Thumbs.db` are skipped when archiving. Save location, what to do
when an archive exists, compression, and extraction behavior can be changed in Settings.

## Build

The core is C++20 (`core/`), wrapped by a native UI on each platform: SwiftUI/AppKit on macOS (`macos/`), Win32 on Windows (`windows/`).

- **macOS** (Xcode, CMake, Ninja): `./scripts/build-macos.sh` produces `build/macos/ZarGUI.app` (universal, arm64 + x86_64).
- **Windows:** run `scripts\build-windows.cmd`. It installs missing tools (Visual Studio C++ build tools, CMake)
  through winget, builds, and writes `build\ZarGUI-Windows-x64.zip` and `build\ZarGUI-Windows-arm64.zip`,
  each containing a single `ZarGUI.exe` with no runtime to install.
- **Core tests:** `cmake -S . -B build/core && cmake --build build/core && ctest --test-dir build/core`

Build output and downloaded dependencies stay in `build/`.

## License

MIT, see [LICENSE](LICENSE). Third-party licenses are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
