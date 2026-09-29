#!/usr/bin/env bash
# Builds build/macos/ZarGUI.app (universal arm64 + x86_64). Everything stays in ./build.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="$ROOT/build"
export CLANG_MODULE_CACHE_PATH="$B/module-cache"
export SWIFTPM_MODULECACHE_OVERRIDE="$B/module-cache"

cmake -S "$ROOT" -B "$B/macos-core" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build "$B/macos-core"

# Merge the core and zstd into the single libzarpack.a the Swift module links.
mkdir -p "$B/macos-lib"
libtool -static -o "$B/macos-lib/libzarpack.a" \
  "$B/macos-core/libzarpack.a" "$B/macos-core/_deps/zstd-build/lib/libzstd.a"

swift build -c release --package-path "$ROOT/macos" \
  --scratch-path "$B/macos-swift" --cache-path "$B/swiftpm-cache" \
  --arch arm64 --arch x86_64 \
  -Xlinker -L"$B/macos-lib"
BIN="$(swift build -c release --package-path "$ROOT/macos" --scratch-path "$B/macos-swift" \
  --cache-path "$B/swiftpm-cache" --arch arm64 --arch x86_64 --show-bin-path)"

APP="$B/macos/ZarGUI.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN/ZarGUI" "$APP/Contents/MacOS/ZarGUI"
cp "$ROOT/macos/Info.plist" "$APP/Contents/Info.plist"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$APP/Contents/Resources/"
[ -f "$ROOT/macos/AppIcon.icns" ] && cp "$ROOT/macos/AppIcon.icns" "$APP/Contents/Resources/"
codesign --force --sign - "$APP"
echo "Built $APP"
